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
#include "archive_file_id.hpp"
#include "archive_header.codec.hpp"
#include "cbor_utils.hpp"
#include "file_descriptor.codec.hpp"
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

    auto sector_device::create_file_secrets2() noexcept
        -> result<file_crypto_ctx::state_type>
    {
        utils::secure_byte_array<32> fileSecret{};
        auto ctrValue = mArchiveSecretCounter.fetch_increment().value();
        VEFS_TRY(crypto::kdf(as_span(fileSecret), master_secret_view(),
                             file_kdf_secret, ctrValue, session_salt_view()));

        crypto::counter::state fileWriteCtrState;
        ctrValue = mArchiveSecretCounter.fetch_increment().value();
        VEFS_TRY(crypto::kdf(as_writable_bytes(as_span(fileWriteCtrState)),
                             master_secret_view(), file_kdf_counter, ctrValue));

        return file_crypto_ctx::state_type{fileSecret,
                                           crypto::counter{fileWriteCtrState}};
    }

    sector_device::sector_device(llfio::mapped_file_handle mfh,
                                 crypto::crypto_provider *cryptoProvider,
                                 size_t const numSectors)
        : mCryptoProvider(cryptoProvider)
        , mArchiveFile(std::move(mfh))
        , mArchiveFileLock(mArchiveFile, llfio::lock_kind::unlocked)
        , mSessionSalt(cryptoProvider->generate_session_salt())
        , mNumSectors(numSectors)
    {
    }

    auto sector_device::open(llfio::mapped_file_handle mfh,
                             crypto::crypto_provider *cryptoProvider,
                             ro_blob<32> userPRK, bool createNew)
        -> result<open_info>
    {

        VEFS_TRY(max_extent, mfh.maximum_extent());
        const size_t numSectors = max_extent / sector_size;

        open_info self{};

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

            OUTCOME_TRY(self.filesystem_index.crypto_state,
                        archive->create_file_secrets2());

            OUTCOME_TRY(self.free_sector_index.crypto_state,
                        archive->create_file_secrets2());
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

            if (auto headerRx = archive->parse_archive_header();
                headerRx.has_value())
            {
                auto &&header = headerRx.assume_value();
                self.filesystem_index =
                    master_file_info(header.filesystem_index);
                self.free_sector_index =
                    master_file_info(header.free_sector_index);
                archive->mArchiveSecretCounter.store(
                    crypto::counter(span(header.archive_secret_counter)));
                archive->mJournalCounter.store(
                    crypto::counter(span(header.journal_counter)));
            }
            else
            {
                return std::move(headerRx).assume_error()
                       << ed::archive_file{"[archive-header]"}
                       << ed::sector_idx{sector_id::master};
            }
        }
        self.device = std::move(archive);
        return std::move(self);
    }

    result<void> sector_device::parse_static_archive_header(ro_blob<32> userPRK)
    {
        auto const staticHeaderSectors =
            mMasterSector.as_span().first(static_header_size);

        dplx::dp::byte_buffer_view mstream(staticHeaderSectors);

        // check for magic number
        if (std::span<std::byte, file_format_id.size()> archivePrefix(
                mstream.consume(file_format_id.size()), file_format_id.size());
            !std::ranges::equal(archivePrefix, std::span(file_format_id)))
        {
            return archive_errc::invalid_prefix;
        }

        VEFS_TRY(staticHeaderBox, crypto::cbor_box_decode_head(mstream));

        if (staticHeaderBox.dataLength > static_cast<int>(static_header_size))
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

    auto sector_device::parse_archive_header(header_id which)
        -> result<archive_header>
    {
        auto const offset = header_offset(which);
        auto const encryptedHeaderArea =
            mMasterSector.as_span().subspan(offset, pheader_size);

        dplx::dp::byte_buffer_view mstream(encryptedHeaderArea);

        VEFS_TRY(headerBox, crypto::cbor_box_decode_head(mstream));

        if (headerBox.dataLength > static_cast<int>(pheader_size))
        {
            return archive_errc::oversized_static_header;
        }

        secure_byte_array<44> keyNonce;
        VEFS_TRY(crypto::kdf(as_span(keyNonce), master_secret_view(),
                             archive_header_kdf_prk, headerBox.salt));

        auto const headerArea = mstream.remaining().first(headerBox.dataLength);

        VEFS_TRY(mCryptoProvider->box_open(headerArea, as_span(keyNonce),
                                           headerArea, headerBox.mac));
        VEFS_SCOPE_EXIT
        {
            utils::secure_memzero(headerArea);
        };

        dplx::dp::const_byte_buffer_view headerStream{headerArea};

        archive_header header{};
        VEFS_TRY(dplx::dp::decode(headerStream, header));

        return std::move(header);
    }

    auto sector_device::parse_archive_header() -> result<archive_header>
    {
        result<archive_header> header[2] = {
            parse_archive_header(header_id::first),
            parse_archive_header(header_id::second)};

        int selector;
        // determine which header to apply
        if (header[0] && header[1])
        {
            VEFS_TRY(cmp, mCryptoProvider->ct_compare(
                              header[0].assume_value().archive_secret_counter,
                              header[1].assume_value().archive_secret_counter));
            if (0 == cmp)
            {
                // both headers are at the same counter value which is an
                // invalid state which cannot be produced by a conforming
                // implementation
                return archive_errc::identical_header_version;
            }

            // select the header with the greater counter value
            selector = 0 < cmp ? 0 : 1;
        }
        else if (header[0])
        {
            selector = 0;
        }
        else if (header[1])
        {
            selector = 1;
        }
        else
        {
            return error{archive_errc::no_archive_header}
                   << ed::wrapped_error{std::move(header[0]).assume_error()};
        }
        return std::move(header[selector]);

        // mFilesystemIndex = std::move(header[selector].filesystem_index);
        // mFreeSectorIndex = std::move(header[selector].free_sector_index);

        // mArchiveSecretCounter.store(
        //    crypto::counter(header[selector].archive_secret_counter));
        // mJournalCounter.store(
        //    crypto::counter(header[selcetor].journal_counter));
        // mHeaderSelector = static_cast<header_id>(selector);
    }

    result<void> sector_device::write_static_archive_header(ro_blob<32> userPRK)
    {
        dplx::dp::byte_buffer_view staticHeaderSectors(
            mMasterSector.as_span().first(static_header_size));

        // insert file format id
        std::memcpy(staticHeaderSectors.consume(file_format_id.size()),
                    file_format_id.data(), file_format_id.size());

        // we need to increment the master key counter _before_ we
        // synthesize the static archive header, because otherwise the
        // counter value used for this encryption round gets serialized
        // and reused.
        auto keyUsageCount = mStaticHeader.master_counter.fetch_increment();

        std::array<std::byte, static_header_size> encodingBuffer;
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

        VEFS_TRY(mCryptoProvider->box_seal(
            rw_dynblob(staticHeaderSectors.consume(encoded.size()),
                       encoded.size()),
            boxHead.mac, key, encoded));

        std::memset(
            staticHeaderSectors.consume(staticHeaderSectors.remaining_size()),
            0, staticHeaderSectors.remaining_size());

        std::shared_lock guard{mSizeSync};
        llfio::io_handle::const_buffer_type writeBuffers[] = {
            {staticHeaderSectors.consumed_begin(),
             static_cast<std::size_t>(staticHeaderSectors.buffer_size())}};
        VEFS_TRY(mArchiveFile.write({writeBuffers, 0}));

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

        std::shared_lock guard{mSizeSync};

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

        std::shared_lock guard{mSizeSync};
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
        std::shared_lock guard{mSizeSync};
        VEFS_TRY_INJECT(
            mArchiveFile.write(offset, {{salt.data(), salt.size()}}),
            ed::sector_idx{sectorIdx});
        return success();
    }

    auto vefs::detail::sector_device::update_header(
        file_crypto_ctx const &filesystemIndexCtx,
        root_sector_info filesystemIndexRoot,
        file_crypto_ctx const &freeSectorIndexCtx,
        root_sector_info freeSectorIndexRoot) -> result<void>
    {
        archive_header assembled{
            .filesystem_index = {detail::file_id::archive_index.as_uuid(),
                                 filesystemIndexCtx, filesystemIndexRoot},
            .free_sector_index = {detail::file_id::free_block_index.as_uuid(),
                                  freeSectorIndexCtx, freeSectorIndexRoot}};

        // fetch a counter value before serialization for header encryption
        auto const ectr = mArchiveSecretCounter.fetch_increment().value();

        vefs::copy(as_bytes(mArchiveSecretCounter.fetch_increment().view()),
                   span(assembled.archive_secret_counter));
        vefs::copy(as_bytes(mJournalCounter.fetch_increment().view()),
                   span(assembled.journal_counter));

        switch_header();

        std::byte serializationMemory[pheader_size];
        VEFS_SCOPE_EXIT
        {
            utils::secure_memzero(serializationMemory);
        };
        dplx::dp::byte_buffer_view serializationBuffer{
            std::span<std::byte>{serializationMemory}};

        VEFS_TRY(dplx::dp::encode(serializationBuffer, assembled));

        auto const headerOffset = header_offset(mHeaderSelector);
        auto const writeArea =
            mMasterSector.as_span().subspan(headerOffset, pheader_size);

        dplx::dp::byte_buffer_view encryptionBuffer(writeArea);
        VEFS_TRY(box, crypto::cbor_box_layout_head(
                          encryptionBuffer,
                          static_cast<std::uint16_t>(
                              serializationBuffer.consumed_size())));

        VEFS_TRY(crypto::kdf(box.salt, as_bytes(span(ectr)),
                             archive_header_kdf_salt, mSessionSalt));

        secure_byte_array<44> headerKeyNonce;
        VEFS_TRY(crypto::kdf(as_span(headerKeyNonce), master_secret_view(),
                             archive_header_kdf_prk, box.salt));

        VEFS_TRY_INJECT(
            mCryptoProvider->box_seal(encryptionBuffer.remaining(), box.mac,
                                      as_span(headerKeyNonce),
                                      serializationBuffer.consumed()),
            ed::archive_file{"[archive-header]"});

        encryptionBuffer.move_consumer(serializationBuffer.consumed_size());
        std::memset(encryptionBuffer.remaining_begin(), 0,
                    encryptionBuffer.remaining_size());

        std::shared_lock guard{mSizeSync};
        VEFS_TRY_INJECT(mArchiveFile.write(headerOffset, {{writeArea.data(),
                                                           writeArea.size()}}),
                        ed::archive_file{"[archive-header]"});

        return success();
    }

    result<void>
    vefs::detail::sector_device::update_static_header(ro_blob<32> newUserPRK)
    {
        return write_static_archive_header(newUserPRK);
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
