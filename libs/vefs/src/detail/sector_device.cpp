#include "sector_device.hpp"

#include <array>
#include <optional>
#include <random>

#include <dplx/dp/decoder/std_container.hpp>
#include <dplx/dp/streams/memory_input_stream.hpp>
#include <dplx/dp/streams/memory_output_stream.hpp>

#include <vefs/span.hpp>
#include <vefs/utils/misc.hpp>

#include <vefs/utils/secure_allocator.hpp>
#include <vefs/utils/secure_array.hpp>

#include "../crypto/cbor_box.hpp"
#include "../crypto/counter.codec.hpp"
#include "../crypto/kdf.hpp"
#include "../platform/sysrandom.hpp"
#include "cbor_utils.hpp"
#include "proto-helper.hpp"
#include "secure_array.codec.hpp"

namespace dplx::dp
{
    template <input_stream Stream>
    class basic_decoder<vefs::detail::master_header, Stream>
    {
    public:
        using value_type = vefs::detail::master_header;
        inline auto operator()(Stream &inStream, value_type &value) const
            -> result<void>
        {
            DPLX_TRY(auto headerHead,
                     dp::parse_tuple_head(inStream, std::true_type{}));

            if (headerHead.version != 0)
            {
                return errc::item_version_mismatch;
            }
            if (headerHead.num_properties != 2)
            {
                return errc::tuple_size_mismatch;
            }

            std::span secretView(value.master_secret);
            DPLX_TRY(decode(inStream, secretView));
            return decode(inStream, value.master_counter);
        }
    };

    template <output_stream Stream>
    class basic_encoder<vefs::detail::master_header, Stream>
    {
    public:
        using value_type = vefs::detail::master_header;
        inline auto operator()(Stream &outStream, value_type const &value) const
            -> result<void>
        {
            using emit = item_emitter<Stream>;

            DPLX_TRY(emit::array(outStream, 3u));
            DPLX_TRY(emit::integer(outStream, 0u)); // version prop

            DPLX_TRY(encode(outStream, value.master_secret));

            // #TODO span cleanup
            auto counterValue = value.master_counter.load();
            auto counterValueView = counterValue.view();
            std::span<std::byte const, 16> counterView(counterValueView);
            return encode(outStream, counterView);
        }
    };
} // namespace dplx::dp

using namespace vefs::utils; // to be refactored

namespace vefs::detail
{
    namespace
    {
        constexpr auto file_format_id = utils::make_byte_array(
            0x82, 0x4E, 0x0D, 0x0A, 0xAB, 0x7E, 0x7B, 0x76, 0x65, 0x66, 0x73,
            0x7D, 0x7E, 0xBB, 0x0A, 0x1A);

        template <std::size_t N>
        inline auto byte_literal(const char (&arr)[N]) noexcept
        {
            span<const char, N> literalMem{arr};
            return as_bytes(literalMem.template first<N - 1>());
        }

        const auto archive_static_header_kdf_prk =
            byte_literal("vefs/prk/StaticArchiveHeaderPRK");
        const auto archive_static_header_kdf_salt =
            byte_literal("vefs/salt/StaticArchiveHeaderWriteCounter");
        const auto archive_header_kdf_prk =
            byte_literal("vefs/prk/ArchiveHeaderPRK");
        const auto archive_header_kdf_salt =
            byte_literal("vefs/salt/ArchiveSecretCounter");

        const auto archive_secret_counter_kdf =
            byte_literal("vefs/seed/ArchiveSecretCounter");
        const auto archive_journal_counter_kdf =
            byte_literal("vefs/seed/JournalCounter");

        const auto sector_kdf_salt = byte_literal("vefs/salt/Sector-Salt");
        const auto sector_kdf_erase = byte_literal("vefs/erase/Sector");
        const auto sector_kdf_prk = byte_literal("vefs/prk/SectorPRK");

        const auto file_kdf_secret = byte_literal("vefs/seed/FileSecret");
        const auto file_kdf_counter =
            byte_literal("vefs/seed/FileSecretCounter");

#pragma pack(push, 1)

        struct StaticArchiveHeaderPrefix
        {
            std::array<std::byte, 4> magic_number;
            std::array<std::byte, 32> static_header_salt;
            std::array<std::byte, 16> static_header_mac;
            uint32_t static_header_length;
        };
        static_assert(sizeof(StaticArchiveHeaderPrefix) == 56);

        struct ArchiveHeaderPrefix
        {
            std::array<std::byte, 32> header_salt;
            std::array<std::byte, 16> header_mac;
            uint32_t header_length;

            constexpr static auto unencrypted_prefix_size =
                sizeof(ArchiveHeaderPrefix::header_salt) +
                sizeof(ArchiveHeaderPrefix::header_mac);
        };
        static_assert(sizeof(ArchiveHeaderPrefix) == 52);

#pragma pack(pop)
    } // namespace

    void pack(archive_header_content &header, adesso::vefs::ArchiveHeader &hd)
    {
        adesso::vefs::FileDescriptor fd;

        header.filesystem_index.crypto_ctx.pack_to(*hd.mutable_archiveindex());
        header.filesystem_index.tree_info.pack_to(*hd.mutable_archiveindex());

        header.free_sector_index.crypto_ctx.pack_to(
            *hd.mutable_freeblockindex());
        header.free_sector_index.tree_info.pack_to(
            *hd.mutable_freeblockindex());
    }
    void unpack(archive_header_content &header, adesso::vefs::ArchiveHeader &hd)
    {
        header.filesystem_index.crypto_ctx.unpack(*hd.mutable_archiveindex());
        header.filesystem_index.tree_info =
            root_sector_info::unpack_from(*hd.mutable_archiveindex());
        header.free_sector_index.crypto_ctx.unpack(
            *hd.mutable_freeblockindex());
        header.free_sector_index.tree_info =
            root_sector_info::unpack_from(*hd.mutable_freeblockindex());
    }

    auto sector_device::create_file_secrets() noexcept
        -> result<std::unique_ptr<file_crypto_ctx>>
    {
        utils::secure_byte_array<32> fileSecret{};
        auto ctrValue = mArchiveSecretCounter.fetch_increment().value();
        VEFS_TRY(crypto::kdf(as_span(fileSecret), master_secret_view(),
                             file_kdf_secret, ctrValue, session_salt_view()));

        crypto::counter::state fileWriteCtrState;
        ctrValue = mArchiveSecretCounter.fetch_increment().value();
        VEFS_TRY(crypto::kdf(as_writable_bytes(as_span(fileWriteCtrState)),
                             master_secret_view(), file_kdf_counter, ctrValue));

        std::unique_ptr<file_crypto_ctx> ctx{new (std::nothrow) file_crypto_ctx(
            fileSecret, crypto::counter{fileWriteCtrState})};
        if (!ctx)
        {
            return errc::not_enough_memory;
        }
        return std::move(ctx);
    }

    sector_device::sector_device(llfio::mapped_file_handle mfh,
                                 crypto::crypto_provider *cryptoProvider,
                                 const size_t numSectors)
        : mCryptoProvider(cryptoProvider)
        , mArchiveFile(std::move(mfh))
        , mArchiveFileLock(mArchiveFile, llfio::lock_kind::unlocked)
        , mHeaderContent{{file_crypto_ctx::zero_init, {}},
                         {file_crypto_ctx::zero_init, {}}}
        , mSessionSalt(cryptoProvider->generate_session_salt())
        , mNumSectors(numSectors)
    {
    }

    auto sector_device::open(llfio::mapped_file_handle mfh,
                             crypto::crypto_provider *cryptoProvider,
                             ro_blob<32> userPRK, bool createNew)
        -> result<std::unique_ptr<sector_device>>
    {

        VEFS_TRY(max_extent, mfh.maximum_extent());
        const size_t numSectors = max_extent / sector_size;

        std::unique_ptr<sector_device> archive{new (std::nothrow) sector_device(
            std::move(mfh), cryptoProvider, numSectors)};

        if (!archive)
        {
            return errc::not_enough_memory;
        }
        if (!archive->mArchiveFileLock.try_lock())
        {
            return errc::still_in_use;
        }

        VEFS_TRY(archive->mArchiveFile.update_map());

        VEFS_TRY(archive->mMasterSector.resize(sector_size));

        if (createNew)
        {
            VEFS_TRY(archive->resize(1));

            VEFS_TRY(cryptoProvider->random_bytes(
                as_span(archive->mStaticHeader.master_secret)));

            crypto::counter::state counterState;
            VEFS_TRY(cryptoProvider->random_bytes(
                as_writable_bytes(as_span(counterState))));
            archive->mStaticHeader.master_counter.store(
                crypto::counter(counterState));

            std::memset(archive->mMasterSector.as_span().data(), 0,
                        sector_size);

            VEFS_TRY(archive->write_static_archive_header(userPRK));

            VEFS_TRY(filesystemSecrets, archive->create_file_secrets());
            std::destroy_at(
                &archive->mHeaderContent.filesystem_index.crypto_ctx);
            new (&archive->mHeaderContent.filesystem_index.crypto_ctx)
                file_crypto_ctx(span(filesystemSecrets->secret).first<32>(),
                                filesystemSecrets->write_counter.load());

            VEFS_TRY(allocSecrets, archive->create_file_secrets());
            std::destroy_at(
                &archive->mHeaderContent.free_sector_index.crypto_ctx);
            new (&archive->mHeaderContent.free_sector_index.crypto_ctx)
                file_crypto_ctx(span(allocSecrets->secret).first<32>(),
                                allocSecrets->write_counter.load());
        }
        else if (archive->size() < 1)
        {
            // at least the master sector is required
            return archive_errc::no_archive_header;
        }
        else
        {
            auto buffer = archive->mMasterSector.as_span();
            llfio::io_handle::buffer_type masterSectorBuffer[] = {
                {buffer.data(), sector_size}};

            VEFS_TRY(readBuffers,
                     archive->mArchiveFile.read({masterSectorBuffer, 0}));
            if (readBuffers.size() != 1 || readBuffers[0].size() < sector_size)
            {
                return archive_errc::no_archive_header;
            }
            if (readBuffers[0].data() != buffer.data())
            {
                std::memcpy(buffer.data(), readBuffers[0].data(), sector_size);
            }

            VEFS_TRY_INJECT(archive->parse_static_archive_header(userPRK),
                            ed::archive_file{"[archive-static-header]"}
                                << ed::sector_idx{sector_id::master});
            VEFS_TRY_INJECT(archive->parse_archive_header(),
                            ed::archive_file{"[archive-header]"}
                                << ed::sector_idx{sector_id::master});
        }
        return std::move(archive);
    }

    result<void> sector_device::parse_static_archive_header(ro_blob<32> userPRK)
    {
        auto const staticHeaderSectors =
            mMasterSector.as_span().first(8 * 1024);

        dplx::dp::byte_buffer_view mstream(staticHeaderSectors);

        // check for magic number
        if (std::span<std::byte, file_format_id.size()> archivePrefix(
                mstream.consume(file_format_id.size()), file_format_id.size());
            !std::ranges::equal(archivePrefix, std::span(file_format_id)))
        {
            return archive_errc::invalid_prefix;
        }

        VEFS_TRY(staticHeaderBox, crypto::cbor_box_decode_head(mstream));

        if (staticHeaderBox.dataLength > 4 * 1024)
        {
            return archive_errc::oversized_static_header;
        }

        secure_byte_array<44> keyNonce;
        VEFS_TRY(crypto::kdf(as_span(keyNonce), userPRK, staticHeaderBox.salt));

        auto const staticHeader =
            mstream.remaining().first(staticHeaderBox.dataLength);

        if (auto rx =
                mCryptoProvider->box_open(staticHeader, as_span(keyNonce),
                                          staticHeader, staticHeaderBox.mac);
            rx.has_failure())
        {
            if (rx.has_error() &&
                rx.assume_error() == archive_errc::tag_mismatch)
            {
                return error{archive_errc::wrong_user_prk}
                       << ed::wrapped_error{std::move(rx).assume_error()};
            }
            return std::move(rx).as_failure();
        }
        VEFS_SCOPE_EXIT
        {
            utils::secure_memzero(staticHeader);
        };

        dplx::dp::const_byte_buffer_view staticHeaderStream(staticHeader);

        VEFS_TRY(dplx::dp::decode(staticHeaderStream, mStaticHeader));
        return success();
    }

    result<void>
    sector_device::parse_archive_header(std::size_t position, std::size_t size,
                                        adesso::vefs::ArchiveHeader &out)
    {
        using adesso::vefs::ArchiveHeader;

        secure_vector<std::byte> headerAndPaddingMem(size, std::byte{});
        span headerAndPadding{headerAndPaddingMem};

        // create io request
        llfio::io_handle::buffer_type buffer_headerAndPadding = {
            nullptr, headerAndPadding.size()};
        llfio::io_handle::io_request<llfio::io_handle::buffers_type>
            req_headerAndPadding({&buffer_headerAndPadding, 1}, position);
        // read request
        VEFS_TRY(res_buf_headerAndPadding,
                 mArchiveFile.read(req_headerAndPadding));
        // copy buffer into data structure
        std::copy(res_buf_headerAndPadding[0].begin(),
                  res_buf_headerAndPadding[0].end(), headerAndPadding.begin());

        auto archiveHeaderPrefix =
            reinterpret_cast<ArchiveHeaderPrefix *>(headerAndPaddingMem.data());

        secure_byte_array<44> headerKeyNonce;
        VEFS_TRY(crypto::kdf(as_span(headerKeyNonce), master_secret_view(),
                             archive_header_kdf_prk,
                             archiveHeaderPrefix->header_salt));

        auto encryptedHeaderPart = headerAndPadding.subspan(
            ArchiveHeaderPrefix::unencrypted_prefix_size);
        VEFS_TRY(mCryptoProvider->box_open(
            encryptedHeaderPart, as_span(headerKeyNonce), encryptedHeaderPart,
            span(archiveHeaderPrefix->header_mac)));

        if (!parse_blob(out, headerAndPadding.subspan(
                                 sizeof(ArchiveHeaderPrefix),
                                 archiveHeaderPrefix->header_length)))
        {
            erase_secrets(out);
            return archive_errc::invalid_proto;
        }

        // the archive is corrupted if the header message doesn't pass parameter
        // validation simple write failures and incomplete writes are catched by
        // the AE construction
        if (out.archivesecretcounter().size() != 16 ||
            out.journalcounter().size() != 16 || !out.has_archiveindex() ||
            !out.has_freeblockindex())
        {
            erase_secrets(out);
            return archive_errc::incompatible_proto;
        }

        // #TODO further validation
        return success();
    }

    result<void> sector_device::parse_archive_header()
    {
        using adesso::vefs::ArchiveHeader;

        const auto apply = [this](ArchiveHeader &header) {
            unpack(mHeaderContent, header);

            mArchiveSecretCounter = crypto::counter(
                as_bytes(span(header.archivesecretcounter())).first<16>());
            crypto::counter::state journalCtrState;
            copy(as_bytes(span(header.journalcounter())),
                 as_writable_bytes(as_span(journalCtrState)));
            mJournalCounter = crypto::counter(journalCtrState);
        };

        const auto headerSize0 = header_size(header_id::first);
        ArchiveHeader first;
        VEFS_SCOPE_EXIT
        {
            erase_secrets(first);
        };
        auto firstParseResult =
            parse_archive_header(mArchiveHeaderOffset, headerSize0, first);

        const auto headerSize1 = header_size(header_id::second);
        ArchiveHeader second;
        VEFS_SCOPE_EXIT
        {
            erase_secrets(second);
        };
        auto secondParseResult = parse_archive_header(
            mArchiveHeaderOffset + headerSize0, headerSize1, second);

        // determine which header to apply
        if (firstParseResult && secondParseResult)
        {
            VEFS_TRY(cmp, mCryptoProvider->ct_compare(
                              as_bytes(span(first.archivesecretcounter())),
                              as_bytes(span(second.archivesecretcounter()))));
            if (0 == cmp)
            {
                // both headers are at the same counter value which is an
                // invalid state which cannot be produced by a conforming
                // implementation
                return archive_errc::identical_header_version;
            }

            if (0 < cmp)
            {
                mHeaderSelector = header_id::first;
                apply(first);
            }
            else
            {
                mHeaderSelector = header_id::second;
                apply(second);
            }
        }
        else if (firstParseResult)
        {
            mHeaderSelector = header_id::first;
            apply(first);
        }
        else if (secondParseResult)
        {
            mHeaderSelector = header_id::second;
            apply(second);
        }
        else
        {
            return error{archive_errc::no_archive_header} << ed::wrapped_error{
                       std::move(firstParseResult).assume_error()};
        }

        return outcome::success();
    }

    result<void> sector_device::write_static_archive_header(ro_blob<32> userPRK)
    {
        using adesso::vefs::StaticArchiveHeader;

        dplx::dp::byte_buffer_view staticHeaderSectors(
            mMasterSector.as_span().first(2 * 4096));

        // insert file format id
        std::memcpy(staticHeaderSectors.consume(file_format_id.size()),
                    file_format_id.data(), file_format_id.size());

        // we need to increment the master key counter _before_ we
        // synthesize the static archive header, because otherwise the
        // counter value used for this encryption round gets serialized
        // and reused.
        auto keyUsageCount = mStaticHeader.master_counter.fetch_increment();

        std::array<std::byte, 1024> encodingBuffer;
        dplx::dp::byte_buffer_view plainStream(encodingBuffer.data(),
                                               encodingBuffer.size(), 0);
        VEFS_SCOPE_EXIT
        {
            utils::secure_memzero(encodingBuffer);
        };

        VEFS_TRY(dplx::dp::encode(plainStream, mStaticHeader));
        auto const encoded = plainStream.consumed();

        VEFS_TRY(boxHead, crypto::cbor_box_layout_head(
                              staticHeaderSectors,
                              static_cast<std::uint16_t>(encoded.size())));

        VEFS_TRY(crypto::kdf(boxHead.salt, keyUsageCount.view(),
                             archive_static_header_kdf_salt,
                             session_salt_view()));

        secure_byte_array<44> key;
        VEFS_TRY(crypto::kdf(as_span(key), userPRK, boxHead.salt));

        DPLX_TRY(mCryptoProvider->box_seal(
            rw_dynblob(staticHeaderSectors.consume(encoded.size()),
                       encoded.size()),
            boxHead.mac, key, encoded));

        std::memset(
            staticHeaderSectors.consume(staticHeaderSectors.remaining_size()),
            0, staticHeaderSectors.remaining_size());

        llfio::io_handle::const_buffer_type writeBuffers[] = {
            {staticHeaderSectors.consumed_begin(),
             static_cast<std::size_t>(staticHeaderSectors.buffer_size())}};
        DPLX_TRY(mArchiveFile.write({writeBuffers, 0}));

        return success();
    }

    auto sector_device::read_sector(rw_blob<sector_payload_size> contentDest,
                                    const file_crypto_ctx &fileCtx,
                                    sector_id sectorIdx,
                                    ro_blob<16> contentMAC) noexcept
        -> result<void>
    {
        using io_buffer = llfio::io_handle::buffer_type;

        constexpr auto sectorIdxLimit =
            std::numeric_limits<std::uint64_t>::max() / sector_size;
        if (sectorIdx == sector_id::master)
        {
            return errc::invalid_argument;
        }
        if (static_cast<std::uint64_t>(sectorIdx) >= sectorIdxLimit)
        {
            return errc::invalid_argument;
        }
        if (contentDest.size() != sector_device::sector_payload_size)
        {
            return errc::invalid_argument;
        }

        const auto sectorOffset = to_offset(sectorIdx);

        // read request
        io_buffer ioBuffer{nullptr, sector_size};
        if (auto readrx = mArchiveFile.read({{&ioBuffer, 1}, sectorOffset}))
        {
            auto buffers = readrx.assume_value();
            assert(buffers.size() == 1);
            assert(buffers[0].size() == ioBuffer.size());
            assert(buffers[0].data() != nullptr);
            ioBuffer = buffers[0];
        }
        else
        {
            result<void> adaptedrx{std::move(readrx).assume_error()};
            adaptedrx.assume_error() << ed::sector_idx{sectorIdx};
            return adaptedrx;
        }

        VEFS_TRY_INJECT(fileCtx.unseal_sector(contentDest, *mCryptoProvider,
                                              ioBuffer, contentMAC),
                        ed::sector_idx{sectorIdx});

        return outcome::success();
    }

    template <typename file_crypto_ctx_T>
    auto sector_device::write_sector(rw_blob<16> mac,
                                     file_crypto_ctx_T &fileCtx,
                                     sector_id sectorIdx,
                                     ro_blob<sector_payload_size> data) noexcept
        -> result<void>
    {
        constexpr auto sectorIdxLimit =
            std::numeric_limits<std::uint64_t>::max() / sector_size;
        if (sectorIdx == sector_id::master)
        {
            return errc::invalid_argument;
        }
        if (static_cast<std::uint64_t>(sectorIdx) >= sectorIdxLimit)
        {
            return errc::invalid_argument;
        }

        std::array<std::byte, sector_size> ioBuffer;
        VEFS_TRY_INJECT(fileCtx.seal_sector(ioBuffer, mac, *mCryptoProvider,
                                            session_salt_view(), data),
                        ed::sector_idx{sectorIdx});

        const auto sectorOffset = to_offset(sectorIdx);

        VEFS_TRY_INJECT(mArchiveFile.write(
                            sectorOffset, {{ioBuffer.data(), ioBuffer.size()}}),
                        ed::sector_idx{sectorIdx});

        return success();
    }

    auto sector_device::erase_sector(sector_id sectorIdx) noexcept
        -> result<void>
    {
        if (sectorIdx == sector_id::master)
        {
            return errc::invalid_argument;
        }
        std::array<std::byte, 32> salt;
        auto nonce = mEraseCounter.fetch_add(1, std::memory_order_relaxed);
        VEFS_TRY(crypto::kdf(salt, mSessionSalt, ro_blob_cast(nonce),
                             sector_kdf_erase));

        const auto offset = to_offset(sectorIdx);
        VEFS_TRY_INJECT(
            mArchiveFile.write(offset, {{salt.data(), salt.size()}}),
            ed::sector_idx{sectorIdx});
        return success();
    }

    result<void> vefs::detail::sector_device::update_header()
    {
        using adesso::vefs::ArchiveHeader;

        ArchiveHeader headerMsg;
        VEFS_SCOPE_EXIT
        {
            erase_secrets(headerMsg);
        };
        pack(mHeaderContent, headerMsg);

        auto secretCtr = mArchiveSecretCounter.fetch_increment().value();
        auto journalCtr = mJournalCounter.load().value();
        auto journalCtrBytes = as_bytes(as_span(journalCtr));

        auto nextSecretCtr = mArchiveSecretCounter.fetch_increment().value();
        auto nextSecretCtrBytes = as_bytes(as_span(nextSecretCtr));
        headerMsg.set_archivesecretcounter(nextSecretCtrBytes.data(),
                                           nextSecretCtrBytes.size());
        headerMsg.set_journalcounter(journalCtrBytes.data(),
                                     journalCtrBytes.size());

        switch_header();
        const auto headerOffset = header_offset(mHeaderSelector);
        const auto fullHeaderSize = header_size(mHeaderSelector);

        secure_vector<std::byte> headerMem{fullHeaderSize, std::byte{}};

        auto prefix = reinterpret_cast<ArchiveHeaderPrefix *>(headerMem.data());

        prefix->header_length =
            static_cast<std::uint32_t>(headerMsg.ByteSizeLong());
        if (!serialize_to_blob(
                span{headerMem}.subspan(sizeof(ArchiveHeaderPrefix),
                                        prefix->header_length),
                headerMsg))
        {
            return archive_errc::protobuf_serialization_failed;
        }

        VEFS_TRY(crypto::kdf(span{prefix->header_salt},
                             as_bytes(as_span(secretCtr)),
                             archive_header_kdf_salt, mSessionSalt));

        secure_byte_array<44> headerKeyNonce;
        VEFS_TRY(crypto::kdf(as_span(headerKeyNonce), master_secret_view(),
                             archive_header_kdf_prk, prefix->header_salt));

        auto encryptedHeader = span{headerMem}.subspan(
            ArchiveHeaderPrefix::unencrypted_prefix_size);
        VEFS_TRY_INJECT(
            mCryptoProvider->box_seal(encryptedHeader, span{prefix->header_mac},
                                      as_span(headerKeyNonce), encryptedHeader),
            ed::archive_file{"[archive-header]"});

        VEFS_TRY_INJECT(mArchiveFile.write(headerOffset, {{headerMem.data(),
                                                           headerMem.size()}}),
                        ed::archive_file{"[archive-header]"});

        return outcome::success();
    }

    result<void>
    vefs::detail::sector_device::update_static_header(ro_blob<32> newUserPRK)
    {
        VEFS_TRY(write_static_archive_header(newUserPRK));

        // we only need to update one of the two headers as the format is robust
        // enough to deal with the probably corrupt other header
        return update_header();
    }
    template result<void>
    sector_device::write_sector(rw_blob<16> mac, file_crypto_ctx &fileCtx,
                                sector_id sectorIdx,
                                ro_blob<sector_payload_size> data);
    template result<void>
    sector_device::write_sector<file_crypto_ctx_interface>(
        rw_blob<16> mac, file_crypto_ctx_interface &fileCtx,
        sector_id sectorIdx, ro_blob<sector_payload_size> data);

} // namespace vefs::detail
