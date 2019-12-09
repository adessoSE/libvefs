#include "sector_device.hpp"

#include <array>
#include <optional>
#include <random>

#include <boost/uuid/random_generator.hpp>

#include <vefs/span.hpp>
#include <vefs/utils/misc.hpp>
#include <vefs/utils/random.hpp>
#include <vefs/utils/secure_allocator.hpp>
#include <vefs/utils/secure_array.hpp>

#include "../crypto/kdf.hpp"
#include "../platform/sysrandom.hpp"
#include "basic_archive_file_meta.hpp"
#include "proto-helper.hpp"

using namespace vefs::utils;

namespace vefs::detail
{
    namespace
    {
        constexpr std::array<std::byte, 4> archive_magic_number =
            utils::make_byte_array(0x76, 0x65, 0x66, 0x73);
        constexpr span archive_magic_number_view{archive_magic_number};

        template <std::size_t N>
        inline auto byte_literal(const char (&arr)[N]) noexcept
        {
            span literalMem{arr};
            return as_bytes(literalMem.template first<N - 1>());
        }

        const auto archive_static_header_kdf_prk =
            byte_literal(u8"vefs/prk/StaticArchiveHeaderPRK");
        const auto archive_static_header_kdf_salt =
            byte_literal(u8"vefs/salt/StaticArchiveHeaderWriteCounter");
        const auto archive_header_kdf_prk =
            byte_literal(u8"vefs/prk/ArchiveHeaderPRK");
        const auto archive_header_kdf_salt =
            byte_literal(u8"vefs/salt/ArchiveSecretCounter");

        const auto archive_secret_counter_kdf =
            byte_literal(u8"vefs/seed/ArchiveSecretCounter");
        const auto archive_journal_counter_kdf =
            byte_literal(u8"vefs/seed/JournalCounter");

        const auto sector_kdf_salt = byte_literal(u8"vefs/salt/Sector-Salt");
        const auto sector_kdf_erase = byte_literal(u8"vefs/erase/Sector");
        const auto sector_kdf_prk = byte_literal(u8"vefs/prk/SectorPRK");

        const auto file_kdf_secret = byte_literal(u8"vefs/seed/FileSecret");
        const auto file_kdf_counter =
            byte_literal(u8"vefs/seed/FileSecretCounter");

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

    result<void>
    vefs::detail::sector_device::initialize_file(basic_archive_file_meta &file)
    {
        auto ctrValue = mArchiveSecretCounter.fetch_increment().value();
        BOOST_OUTCOME_TRY(crypto::kdf(as_span(file.secret),
                                      master_secret_view(), file_kdf_secret,
                                      ctrValue, session_salt_view()));

        {
            crypto::counter::state fileWriteCtrState;
            ctrValue = mArchiveSecretCounter.fetch_increment().value();
            BOOST_OUTCOME_TRY(
                crypto::kdf(as_writable_bytes(as_span(fileWriteCtrState)),
                            master_secret_view(), file_kdf_counter, ctrValue));
            file.write_counter.store(crypto::counter{fileWriteCtrState});
        }

        file.start_block_idx = sector_id{};
        file.start_block_mac = {};
        file.size = 0;
        file.tree_depth = -1;

        return outcome::success();
    }

    auto sector_device::create_file() noexcept
        -> result<basic_archive_file_meta>
    {
        thread_local utils::xoroshiro128plus fileid_prng = []() {
            std::array<std::uint64_t, 2> randomState{};
            auto rx = random_bytes(as_writable_bytes(span(randomState)));
            if (!rx)
            {
                throw error_exception{rx.assume_error()};
            }

            return xoroshiro128plus{randomState[0], randomState[1]};
        }();
        thread_local boost::uuids::basic_random_generator generate_fileid{
            fileid_prng};

        basic_archive_file_meta file;

        file.id = file_id{generate_fileid()};

        BOOST_OUTCOME_TRY(initialize_file(file));

        return std::move(file);
    }

    sector_device::sector_device(llfio::mapped_file_handle mfh,
                                 crypto::crypto_provider *cryptoProvider,
                                 const size_t numSectors)
        : mCryptoProvider(cryptoProvider)
        , mArchiveFile(std::move(mfh))
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

        if (createNew)
        {
            BOOST_OUTCOME_TRY(archive->resize(1));

            BOOST_OUTCOME_TRY(cryptoProvider->random_bytes(
                as_span(archive->mArchiveMasterSecret)));
            BOOST_OUTCOME_TRY(cryptoProvider->random_bytes(
                as_span(archive->mStaticHeaderWriteCounter)));

            BOOST_OUTCOME_TRY(archive->write_static_archive_header(userPRK));

            archive->mFreeBlockIdx =
                std::make_unique<basic_archive_file_meta>();
            archive->mFreeBlockIdx->id = file_id::free_block_index;
            BOOST_OUTCOME_TRY(
                archive->initialize_file(*archive->mFreeBlockIdx));

            archive->mArchiveIdx = std::make_unique<basic_archive_file_meta>();
            archive->mArchiveIdx->id = file_id::archive_index;
            BOOST_OUTCOME_TRY(archive->initialize_file(*archive->mArchiveIdx));
        }
        else if (archive->size() < 1)
        {
            // at least the master sector is required
            return archive_errc::no_archive_header;
        }
        else
        {
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
        StaticArchiveHeaderPrefix archivePrefix{};
        // #UB-ObjectLifetime

        // create io request
        llfio::io_handle::buffer_type buffer_magic_number = {
            nullptr, sizeof(StaticArchiveHeaderPrefix::magic_number)};
        llfio::io_handle::buffer_type buffer_static_header_salt = {
            nullptr, sizeof(StaticArchiveHeaderPrefix::static_header_salt)};
        llfio::io_handle::buffer_type buffer_static_header_mac = {
            nullptr, sizeof(StaticArchiveHeaderPrefix::static_header_mac)};
        llfio::io_handle::buffer_type buffer_static_header_length = {
            nullptr, sizeof(StaticArchiveHeaderPrefix::static_header_length)};
        std::array<llfio::io_handle::buffer_type, 4> buffers = {
            buffer_magic_number, buffer_static_header_salt,
            buffer_static_header_mac, buffer_static_header_length};
        llfio::io_handle::io_request<llfio::io_handle::buffers_type>
            req_archivePrefix(buffers, 0);
        // read request
        VEFS_TRY(res_buf_archivePrefix, mArchiveFile.read(req_archivePrefix));
        // copy buffer into data structure
        std::copy(res_buf_archivePrefix[0].begin(),
                  res_buf_archivePrefix[0].end(),
                  archivePrefix.magic_number.begin());
        std::copy(res_buf_archivePrefix[1].begin(),
                  res_buf_archivePrefix[1].end(),
                  archivePrefix.static_header_salt.begin());
        std::copy(res_buf_archivePrefix[2].begin(),
                  res_buf_archivePrefix[2].end(),
                  archivePrefix.static_header_mac.begin());
        memcpy(&archivePrefix.static_header_length,
               res_buf_archivePrefix[3].data(),
               res_buf_archivePrefix[3].size());

        // check for magic number
        if (!equal(span(archivePrefix.magic_number), archive_magic_number_view))
        {
            return archive_errc::invalid_prefix;
        }
        // the static archive header must be within the bounds of the first
        // block
        if (archivePrefix.static_header_length >=
            sector_size - sizeof(StaticArchiveHeaderPrefix))
        {
            return archive_errc::oversized_static_header;
        }

        secure_byte_array<512> staticHeaderStack;
        secure_vector<std::byte> staticHeaderHeap;

        auto staticHeader =
            archivePrefix.static_header_length <= staticHeaderStack.size()
                ? as_span(staticHeaderStack)
                      .subspan(0, archivePrefix.static_header_length)
                : (staticHeaderHeap.resize(archivePrefix.static_header_length),
                   span(staticHeaderHeap));

        // create io request
        llfio::io_handle::buffer_type buffer_staticHeader = {
            nullptr, staticHeader.size()};
        llfio::io_handle::io_request<llfio::io_handle::buffers_type>
            req_staticHeader(buffer_staticHeader,
                             sizeof(StaticArchiveHeaderPrefix));
        // read request
        VEFS_TRY(res_buf_staticHeader, mArchiveFile.read(req_staticHeader));
        // copy buffer into data structure
        std::copy(res_buf_staticHeader[0].begin(),
                  res_buf_staticHeader[0].end(), staticHeader.begin());

        secure_byte_array<44> keyNonce;
        BOOST_OUTCOME_TRY(crypto::kdf(as_span(keyNonce), userPRK,
                                      archivePrefix.static_header_salt));

        if (auto rx = mCryptoProvider->box_open(
                staticHeader, as_span(keyNonce), staticHeader,
                span(archivePrefix.static_header_mac));
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

        adesso::vefs::StaticArchiveHeader staticHeaderMsg;
        VEFS_SCOPE_EXIT
        {
            erase_secrets(staticHeaderMsg);
        };

        if (!parse_blob(staticHeaderMsg, staticHeader))
        {
            return archive_errc::invalid_proto;
        }
        if (staticHeaderMsg.formatversion() != 0)
        {
            return archive_errc::unknown_format_version;
        }
        if (staticHeaderMsg.mastersecret().size() != 64 ||
            staticHeaderMsg.staticarchiveheaderwritecounter().size() != 16)
        {
            return archive_errc::incompatible_proto;
        }

        copy(as_bytes(span(staticHeaderMsg.mastersecret())),
             as_span(mArchiveMasterSecret));
        copy(as_bytes(span(staticHeaderMsg.staticarchiveheaderwritecounter())),
             as_span(mStaticHeaderWriteCounter));

        mArchiveHeaderOffset = sizeof(StaticArchiveHeaderPrefix) +
                               archivePrefix.static_header_length;

        return outcome::success();
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
            req_headerAndPadding(buffer_headerAndPadding, position);
        // read request
        VEFS_TRY(res_buf_headerAndPadding,
                 mArchiveFile.read(req_headerAndPadding));
        // copy buffer into data structure
        std::copy(res_buf_headerAndPadding[0].begin(),
                  res_buf_headerAndPadding[0].end(), headerAndPadding.begin());

        auto archiveHeaderPrefix =
            reinterpret_cast<ArchiveHeaderPrefix *>(headerAndPaddingMem.data());

        secure_byte_array<44> headerKeyNonce;
        BOOST_OUTCOME_TRY(crypto::kdf(
            as_span(headerKeyNonce), master_secret_view(),
            archive_header_kdf_prk, archiveHeaderPrefix->header_salt));

        auto encryptedHeaderPart = headerAndPadding.subspan(
            ArchiveHeaderPrefix::unencrypted_prefix_size);
        BOOST_OUTCOME_TRY(mCryptoProvider->box_open(
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
            return error{archive_errc::incompatible_proto};
        }

        // #TODO further validation
        return outcome::success();
    }

    result<void> sector_device::parse_archive_header()
    {
        using adesso::vefs::ArchiveHeader;

        const auto apply = [this](ArchiveHeader &header) {
            mArchiveIdx = unpack(*header.mutable_archiveindex());
            mFreeBlockIdx = unpack(*header.mutable_freeblockindex());

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
            BOOST_OUTCOME_TRY(
                cmp, mCryptoProvider->ct_compare(
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

        StaticArchiveHeaderPrefix headerPrefix{};
        copy(archive_magic_number_view, span(headerPrefix.magic_number));

        StaticArchiveHeader header;
        header.set_formatversion(0);
        VEFS_SCOPE_EXIT
        {
            erase_secrets(header);
        };

        copy((++crypto::counter{as_span(mStaticHeaderWriteCounter)}).view(),
             as_writable_bytes(as_span(mStaticHeaderWriteCounter)));

        header.set_staticarchiveheaderwritecounter(std::string{
            reinterpret_cast<char *>(mStaticHeaderWriteCounter.data()),
            mStaticHeaderWriteCounter.size()});

        BOOST_OUTCOME_TRY(crypto::kdf(span(headerPrefix.static_header_salt),
                                      as_span(mStaticHeaderWriteCounter),
                                      archive_static_header_kdf_salt,
                                      session_salt_view()));

        header.set_mastersecret(
            std::string{reinterpret_cast<char *>(mArchiveMasterSecret.data()),
                        mArchiveMasterSecret.size()});

        headerPrefix.static_header_length =
            static_cast<uint32_t>(header.ByteSizeLong());
        secure_vector<std::byte> msgHolder{headerPrefix.static_header_length,
                                           std::byte{}};
        span msg{msgHolder};

        if (!serialize_to_blob(msg, header))
        {
            return archive_errc::protobuf_serialization_failed;
        }

        secure_byte_array<44> key;
        BOOST_OUTCOME_TRY(crypto::kdf(as_span(key), userPRK,
                                      headerPrefix.static_header_salt));

        BOOST_OUTCOME_TRY(mCryptoProvider->box_seal(
            msg, span{headerPrefix.static_header_mac}, as_span(key), msg));

        VEFS_TRY(mArchiveFile.write(
            0,
            {{headerPrefix.magic_number.data(),
              headerPrefix.magic_number.size()},
             {headerPrefix.static_header_salt.data(),
              headerPrefix.static_header_salt.size()},
             {headerPrefix.static_header_mac.data(),
              headerPrefix.static_header_mac.size()},
             {reinterpret_cast<std::byte *>(&headerPrefix.static_header_length),
              sizeof(headerPrefix.static_header_length)}}));

        VEFS_TRY(mArchiveFile.write(sizeof(headerPrefix),
                                    {{msg.data(), msg.size()}}));

        mArchiveHeaderOffset =
            sizeof(headerPrefix) + headerPrefix.static_header_length;

        return outcome::success();
    }

    result<void> sector_device::read_sector(rw_blob<sector_payload_size> buffer,
                                            const basic_archive_file_meta &file,
                                            sector_id sectorIdx,
                                            ro_dynblob contentMAC)
    {
        const auto sectorOffset = to_offset(sectorIdx);

        if (buffer.size() != sector_device::sector_payload_size)
        {
            return errc::invalid_argument;
        }

        secure_byte_array<32> sectorSaltMem;
        span sectorSalt = as_span(sectorSaltMem);

        // create io request
        llfio::io_handle::buffer_type buffer_sectorSalt = {nullptr,
                                                           sectorSalt.size()};
        llfio::io_handle::io_request<llfio::io_handle::buffers_type>
            req_sectorSalt(buffer_sectorSalt, sectorOffset);
        // read request
        VEFS_TRY(res_buf_sectorSalt, mArchiveFile.read(req_sectorSalt));
        // copy buffer into data structure
        std::copy(res_buf_sectorSalt[0].begin(), res_buf_sectorSalt[0].end(),
                  sectorSalt.data());

        // create io request
        llfio::io_handle::buffer_type buffer_payload = {nullptr, buffer.size()};
        llfio::io_handle::io_request<llfio::io_handle::buffers_type>
            req_payload(buffer_payload, sectorOffset + sectorSalt.size());
        // read request
        VEFS_TRY(res_buf_payload, mArchiveFile.read(req_payload));
        // copy buffer into data structure
        std::copy(res_buf_payload[0].begin(), res_buf_payload[0].end(),
                  buffer.data());

        secure_byte_array<44> sectorKeyNonce;
        BOOST_OUTCOME_TRY(crypto::kdf(as_span(sectorKeyNonce),
                                      file.secret_view(), sector_kdf_prk,
                                      sectorSalt));

        if (auto decrx = mCryptoProvider->box_open(
                buffer, as_span(sectorKeyNonce), buffer, contentMAC);
            decrx.has_failure())
        {
            return std::move(decrx).assume_error() << ed::sector_idx{sectorIdx};
        }
        return outcome::success();
    }

    result<void> detail::sector_device::write_sector(
        rw_blob<sector_payload_size> ciphertextBuffer, rw_dynblob mac,
        basic_archive_file_meta &file, sector_id sectorIdx,
        ro_blob<sector_payload_size> data)
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
        if (data.size() != sector_device::sector_payload_size)
        {
            return errc::invalid_argument;
        }
        if (ciphertextBuffer.size() != sector_device::sector_payload_size)
        {
            return errc::invalid_argument;
        }

        std::array<std::byte, 32> salt{};
        auto nonce = file.write_counter.fetch_increment();
        BOOST_OUTCOME_TRY(crypto::kdf(span(salt), nonce.view(), sector_kdf_salt,
                                      mSessionSalt));

        secure_byte_array<44> sectorKeyNonce;
        BOOST_OUTCOME_TRY(crypto::kdf(
            as_span(sectorKeyNonce), file.secret_view(), sector_kdf_prk, salt));

        VEFS_TRY_INJECT(mCryptoProvider->box_seal(ciphertextBuffer, mac,
                                                  as_span(sectorKeyNonce),
                                                  data),
                        ed::sector_idx{sectorIdx});

        const auto sectorOffset = to_offset(sectorIdx);

        VEFS_TRY_INJECT(
            mArchiveFile.write(sectorOffset, {{salt.data(), salt.size()}}),
            ed::sector_idx{sectorIdx});

        VEFS_TRY_INJECT(mArchiveFile.write(sectorOffset + salt.size(),
                                           {{ciphertextBuffer.data(),
                                             ciphertextBuffer.size()}}),
                        ed::sector_idx{sectorIdx});

        return outcome::success();
    }

    result<void>
    vefs::detail::sector_device::erase_sector(basic_archive_file_meta &file,
                                              sector_id sectorIdx)
    {
        if (sectorIdx == sector_id::master)
        {
            return errc::invalid_argument;
        }
        std::array<std::byte, 32> saltBuffer{};
        span salt{saltBuffer};

        auto nonce = file.write_counter.fetch_increment();
        BOOST_OUTCOME_TRY(
            crypto::kdf(salt, nonce.view(), sector_kdf_erase, mSessionSalt));

        const auto offset = to_offset(sectorIdx);

        VEFS_TRY(mArchiveFile.write(offset, {{salt.data(), salt.size()}}));

        return outcome::success();
    }

    result<void> vefs::detail::sector_device::update_header()
    {
        using adesso::vefs::ArchiveHeader;

        ArchiveHeader headerMsg;
        VEFS_SCOPE_EXIT
        {
            erase_secrets(headerMsg);
        };
        headerMsg.set_allocated_archiveindex(pack(*mArchiveIdx));
        headerMsg.set_allocated_freeblockindex(pack(*mFreeBlockIdx));

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

        BOOST_OUTCOME_TRY(crypto::kdf(span{prefix->header_salt},
                                      as_bytes(as_span(secretCtr)),
                                      archive_header_kdf_salt, mSessionSalt));

        secure_byte_array<44> headerKeyNonce;
        BOOST_OUTCOME_TRY(
            crypto::kdf(as_span(headerKeyNonce), master_secret_view(),
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
        BOOST_OUTCOME_TRY(write_static_archive_header(newUserPRK));

        // we only need to update one of the two headers as the format is robust
        // enough to deal with the probably corrupt other header
        return update_header();
    }
} // namespace vefs::detail
