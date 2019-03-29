#include "precompiled.hpp"
#include <vefs/detail/raw_archive.hpp>

#include <array>
#include <random>
#include <optional>

#include <boost/uuid/random_generator.hpp>

#include <vefs/exceptions.hpp>
#include <vefs/crypto/kdf.hpp>
#include <vefs/utils/misc.hpp>
#include <vefs/utils/random.hpp>
#include <vefs/utils/secure_array.hpp>
#include <vefs/utils/secure_allocator.hpp>
#include <vefs/detail/basic_archive_file_meta.hpp>

#include "proto-helper.hpp"
#include "sysrandom.hpp"

using namespace vefs::utils;


namespace vefs::detail
{
    const file_id file_id::archive_index{ utils::uuid{ 0xba, 0x22, 0xb0, 0x33, 0x4b, 0xa8, 0x4e, 0x5b, 0x83, 0x0c, 0xbf, 0x48, 0x94, 0xaf, 0x53, 0xf8 } };
    const file_id file_id::free_block_index{ utils::uuid{ 0x33, 0x38, 0xbe, 0x54, 0x6b, 0x02, 0x49, 0x24, 0x9f, 0xcc, 0x56, 0x3d, 0x7e, 0xe6, 0x81, 0xe6 }  };

    namespace
    {
        constexpr std::array<std::byte, 4> archive_magic_number = 0x76'65'66'73_as_bytes;
        constexpr blob_view archive_magic_number_view = blob_view{ archive_magic_number };

        const blob_view archive_static_header_kdf_prk = "vefs/prk/StaticArchiveHeaderPRK"_bv;
        const blob_view archive_static_header_kdf_salt = "vefs/salt/StaticArchiveHeaderWriteCounter"_bv;
        const blob_view archive_header_kdf_prk = "vefs/prk/ArchiveHeaderPRK"_bv;
        const blob_view archive_header_kdf_salt = "vefs/salt/ArchiveSecretCounter"_bv;

        const blob_view archive_secret_counter_kdf = "vefs/seed/ArchiveSecretCounter"_bv;
        const blob_view archive_journal_counter_kdf = "vefs/seed/JournalCounter"_bv;

        const blob_view sector_kdf_salt = "vefs/salt/Sector-Salt"_bv;
        const blob_view sector_kdf_erase = "vefs/erase/Sector"_bv;
        const blob_view sector_kdf_prk = "vefs/prk/SectorPRK"_bv;

        const blob_view file_kdf_secret = "vefs/seed/FileSecret"_bv;
        const blob_view file_kdf_counter = "vefs/seed/FileSecretCounter"_bv;



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


            constexpr static auto unencrypted_prefix_size
                = sizeof(ArchiveHeaderPrefix::header_salt)
                + sizeof(ArchiveHeaderPrefix::header_mac);
        };
        static_assert(sizeof(ArchiveHeaderPrefix) == 52);

#pragma pack(pop)
    }

    result<void> vefs::detail::raw_archive::initialize_file(basic_archive_file_meta &file)
    {
        auto ctrValue = mArchiveSecretCounter.fetch_increment().value();
        OUTCOME_TRY(crypto::kdf(blob{ file.secret }, master_secret_view(),
            file_kdf_secret, ctrValue, session_salt_view()));

        {
            crypto::counter::state fileWriteCtrState;
            ctrValue = mArchiveSecretCounter.fetch_increment().value();
            OUTCOME_TRY(crypto::kdf(blob{ fileWriteCtrState }, master_secret_view(),
                file_kdf_counter, ctrValue));
            file.write_counter.store(crypto::counter{ fileWriteCtrState });
        }

        file.start_block_idx = sector_id{};
        file.start_block_mac = {};
        file.size = 0;
        file.tree_depth = -1;

        return outcome::success();
    }

    auto raw_archive::create_file() noexcept
        -> result<basic_archive_file_meta>
    {
        thread_local utils::xoroshiro128plus fileid_prng = []()
        {
            std::array<std::uint64_t, 2> randomState;
            auto rx = random_bytes(as_blob(randomState));
            if (!rx)
            {
                throw error_exception{ rx.assume_error() };
            }

            return xoroshiro128plus{ randomState[0], randomState[1] };
        }();
        thread_local boost::uuids::basic_random_generator generate_fileid{ fileid_prng };

        basic_archive_file_meta file;

        file.id = file_id{ generate_fileid() };

        OUTCOME_TRY(initialize_file(file));

        return std::move(file);
    }

    raw_archive::raw_archive(file::ptr archiveFile, crypto::crypto_provider * cryptoProvider)
        : mCryptoProvider(cryptoProvider)
        , mArchiveFile(std::move(archiveFile))
        , mSessionSalt(cryptoProvider->generate_session_salt())
        , mNumSectors(0)
    {
        mNumSectors = mArchiveFile->size() / sector_size;
    }

    auto raw_archive::open(filesystem::ptr fs, std::string_view path,
        crypto::crypto_provider * cryptoProvider, blob_view userPRK, file_open_mode_bitset openMode)
        -> result<std::unique_ptr<raw_archive>>
    {
        // no read only support as of now
        openMode |= file_open_mode::readwrite;
        const auto create = openMode % file_open_mode::create;
        if (create)
        {
            openMode |= file_open_mode::truncate;
        }

        std::error_code scode;
        auto file = fs->open(path, openMode, scode);
        if (!file)
        {
            return error{ scode };
        }

        std::unique_ptr<raw_archive> archive{
            new(std::nothrow) raw_archive(std::move(file), cryptoProvider)
        };
        if (!archive)
        {
            return errc::not_enough_memory;
        }

        if (create)
        {
            OUTCOME_TRY(archive->resize(1));

            OUTCOME_TRY(cryptoProvider->random_bytes(blob{ archive->mArchiveMasterSecret }));
            OUTCOME_TRY(cryptoProvider->random_bytes(blob{ archive->mStaticHeaderWriteCounter }));

            OUTCOME_TRY(archive->write_static_archive_header(userPRK));

            archive->mFreeBlockIdx = std::make_unique<basic_archive_file_meta>();
            archive->mFreeBlockIdx->id = file_id::free_block_index;
            OUTCOME_TRY(archive->initialize_file(*archive->mFreeBlockIdx));

            archive->mArchiveIdx = std::make_unique<basic_archive_file_meta>();
            archive->mArchiveIdx->id = file_id::archive_index;
            OUTCOME_TRY(archive->initialize_file(*archive->mArchiveIdx));
        }
        else if (archive->size() < 1)
        {
            // at least the master sector is required
            return archive_errc::no_archive_header;
        }
        else
        {
            VEFS_TRY_INJECT(archive->parse_static_archive_header(userPRK),
                ed::archive_file{ "[archive-static-header]" }
                << ed::sector_idx{ sector_id::master });
            VEFS_TRY_INJECT(archive->parse_archive_header(),
                ed::archive_file{ "[archive-header]" }
                << ed::sector_idx{ sector_id::master });
        }
        return std::move(archive);
    }

    result<void> raw_archive::sync()
    {
        std::error_code scode;
        mArchiveFile->sync(scode);
        if (scode)
        {
            return error{ scode };
        }
        return outcome::success();
    }

    result<void> raw_archive::parse_static_archive_header(blob_view userPRK)
    {
        std::error_code scode;

        StaticArchiveHeaderPrefix archivePrefix;
        mArchiveFile->read(as_blob(archivePrefix), 0, scode);
        if (scode)
        {
            return error{ scode };
        }

        // check for magic number
        if (!equal(blob_view{ archivePrefix.magic_number }, archive_magic_number_view))
        {
            return archive_errc::invalid_prefix;
        }
        // the static archive header must be within the bounds of the first block
        if (archivePrefix.static_header_length >= sector_size - sizeof(StaticArchiveHeaderPrefix))
        {
            return archive_errc::oversized_static_header;
        }

        secure_byte_array<512> staticHeaderStack;
        secure_vector<std::byte> staticHeaderHeap;

        auto staticHeader = archivePrefix.static_header_length <= staticHeaderStack.size()
            ? blob{ staticHeaderStack }.slice(0, archivePrefix.static_header_length)
            : (staticHeaderHeap.resize(archivePrefix.static_header_length), blob{ staticHeaderHeap });

        mArchiveFile->read(staticHeader, sizeof(StaticArchiveHeaderPrefix), scode);
        if (scode)
        {
            return error{ scode };
        }

        secure_byte_array<44> keyNonce;
        OUTCOME_TRY(crypto::kdf(blob{ keyNonce }, userPRK, archivePrefix.static_header_salt));

        if (auto rx = mCryptoProvider->box_open(staticHeader, blob_view{ keyNonce },
            staticHeader, blob_view{ archivePrefix.static_header_mac });
            rx.has_failure())
        {
            if (rx.has_error() && rx.assume_error() == archive_errc::tag_mismatch)
            {
                return error{ archive_errc::wrong_user_prk }
                    << ed::wrapped_error{ std::move(rx).assume_error() };
            }
            return std::move(rx).as_failure();
        }

        adesso::vefs::StaticArchiveHeader staticHeaderMsg;
        VEFS_SCOPE_EXIT { erase_secrets(staticHeaderMsg); };

        if (!parse_blob(staticHeaderMsg, staticHeader))
        {
            return archive_errc::invalid_proto;
        }
        if (staticHeaderMsg.formatversion() != 0)
        {
            return archive_errc::unknown_format_version;
        }
        if (staticHeaderMsg.mastersecret().size() != 64
            || staticHeaderMsg.staticarchiveheaderwritecounter().size() != 16)
        {
            return archive_errc::incompatible_proto;
        }

        blob_view{ staticHeaderMsg.mastersecret() }.copy_to(blob{ mArchiveMasterSecret });
        blob_view{ staticHeaderMsg.staticarchiveheaderwritecounter() }
            .copy_to(blob{ mStaticHeaderWriteCounter });

        mArchiveHeaderOffset = sizeof(StaticArchiveHeaderPrefix) + archivePrefix.static_header_length;

        return outcome::success();
    }

    result<void> raw_archive::parse_archive_header(std::size_t position, std::size_t size,
        adesso::vefs::ArchiveHeader &out)
    {
        using adesso::vefs::ArchiveHeader;
        std::error_code scode;

        secure_vector<std::byte> headerAndPaddingMem(size, std::byte{});
        blob headerAndPadding{ headerAndPaddingMem };

        mArchiveFile->read(headerAndPadding, position, scode);
        if (scode)
        {
            return error{ scode };
        }

        auto archiveHeaderPrefix = reinterpret_cast<ArchiveHeaderPrefix *>(headerAndPaddingMem.data());

        secure_byte_array<44> headerKeyNonce;
        OUTCOME_TRY(crypto::kdf(blob{ headerKeyNonce }, master_secret_view(),
            archive_header_kdf_prk, archiveHeaderPrefix->header_salt));

        auto encryptedHeaderPart = headerAndPadding.slice(ArchiveHeaderPrefix::unencrypted_prefix_size);
        OUTCOME_TRY(mCryptoProvider->box_open(encryptedHeaderPart, blob{ headerKeyNonce },
            encryptedHeaderPart, blob_view{ archiveHeaderPrefix->header_mac }));


        if (!parse_blob(out, headerAndPadding.slice(sizeof(ArchiveHeaderPrefix),
                        archiveHeaderPrefix->header_length)))
        {
            erase_secrets(out);
            return archive_errc::invalid_proto;
        }

        // the archive is corrupted if the header message doesn't pass parameter validation
        // simple write failures and incomplete writes are catched by the AE construction
        if (out.archivesecretcounter().size() != 16
            || out.journalcounter().size() != 16
            || !out.has_archiveindex()
            || !out.has_freeblockindex())
        {
            erase_secrets(out);
            return error{ archive_errc::incompatible_proto };
        }

        // #TODO further validation
        return outcome::success();
    }

    result<void> raw_archive::parse_archive_header()
    {
        using adesso::vefs::ArchiveHeader;

        const auto apply = [this](ArchiveHeader &header)
        {
            mArchiveIdx = unpack(*header.mutable_archiveindex());
            mFreeBlockIdx = unpack(*header.mutable_freeblockindex());

            mArchiveSecretCounter = crypto::counter(blob_view{ header.archivesecretcounter() });
            crypto::counter::state journalCtrState;
            blob_view{ header.journalcounter() }.copy_to(blob{ journalCtrState });
            mJournalCounter = crypto::counter(journalCtrState);
        };


        const auto headerSize0 = header_size(header_id::first);
        ArchiveHeader first;
        VEFS_SCOPE_EXIT{ erase_secrets(first); };
        auto firstParseResult = parse_archive_header(mArchiveHeaderOffset, headerSize0, first);

        const auto headerSize1 = header_size(header_id::second);
        ArchiveHeader second;
        VEFS_SCOPE_EXIT{ erase_secrets(second); };
        auto secondParseResult
            = parse_archive_header(mArchiveHeaderOffset + headerSize0, headerSize1, second);

        // determine which header to apply
        if (firstParseResult && secondParseResult)
        {
            OUTCOME_TRY(cmp, mCryptoProvider->ct_compare(
                blob_view{ first.archivesecretcounter() },
                blob_view{ second.archivesecretcounter() }
            ));
            if (0 == cmp)
            {
                // both headers are at the same counter value which is an invalid
                // state which cannot be produced by a conforming implementation
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
            return error{ archive_errc::no_archive_header }
                << ed::wrapped_error{ std::move(firstParseResult).assume_error() };
        }

        return outcome::success();
    }

    result<void> raw_archive::write_static_archive_header(blob_view userPRK)
    {
        using adesso::vefs::StaticArchiveHeader;

        StaticArchiveHeaderPrefix headerPrefix;
        archive_magic_number_view.copy_to(blob{ headerPrefix.magic_number });

        StaticArchiveHeader header;
        header.set_formatversion(0);
        VEFS_SCOPE_EXIT{ erase_secrets(header); };

        (++crypto::counter{ mStaticHeaderWriteCounter })
            .view()
            .copy_to(blob{ mStaticHeaderWriteCounter });

        header.set_staticarchiveheaderwritecounter(
            std::string{ reinterpret_cast<char *>(mStaticHeaderWriteCounter.data()), mStaticHeaderWriteCounter.size() }
        );

        OUTCOME_TRY(crypto::kdf(blob{ headerPrefix.static_header_salt },
            blob_view{ mStaticHeaderWriteCounter },
            archive_static_header_kdf_salt, session_salt_view()));

        header.set_mastersecret(
            std::string{ reinterpret_cast<char *>(mArchiveMasterSecret.data()), mArchiveMasterSecret.size() }
        );

        headerPrefix.static_header_length = static_cast<uint32_t>(header.ByteSizeLong());
        secure_vector<std::byte> msgHolder{ headerPrefix.static_header_length, std::byte{} };
        blob msg{ msgHolder };

        if (!serialize_to_blob(msg, header))
        {
            return archive_errc::protobuf_serialization_failed;
        }

        secure_byte_array<44> key;
        OUTCOME_TRY(crypto::kdf(blob{ key }, userPRK, headerPrefix.static_header_salt));

        OUTCOME_TRY(mCryptoProvider->box_seal(msg, blob{ headerPrefix.static_header_mac },
            blob_view{ key }, msg));

        std::error_code scode;
        mArchiveFile->write(as_blob_view(headerPrefix), 0, scode);
        if (scode)
        {
            return error{ scode };
        }
        mArchiveFile->write(msg, sizeof(headerPrefix), scode);
        if (scode)
        {
            return error{ scode };
        }

        mArchiveHeaderOffset = sizeof(headerPrefix) + headerPrefix.static_header_length;

        return outcome::success();
    }

    result<void> raw_archive::read_sector(blob buffer, const basic_archive_file_meta & file,
        sector_id sectorIdx, blob_view contentMAC)
    {
        const auto sectorOffset = to_offset(sectorIdx);

        if (buffer.size() != raw_archive::sector_payload_size)
        {
            return errc::invalid_argument;
        }

        secure_byte_array<32> sectorSaltMem;
        blob sectorSalt{ sectorSaltMem };

        std::error_code scode;
        mArchiveFile->read(sectorSalt, sectorOffset, scode);
        if (scode)
        {
            return error{ scode }
                << ed::sector_idx{ sectorIdx };
        }
        mArchiveFile->read(buffer, sectorOffset + sectorSalt.size(), scode);
        if (scode)
        {
            return error{ scode }
                << ed::sector_idx{ sectorIdx };
        }

        secure_byte_array<44> sectorKeyNonce;
        OUTCOME_TRY(crypto::kdf(blob{ sectorKeyNonce },
            file.secret_view(), sector_kdf_prk, sectorSalt));

        if (auto decrx = mCryptoProvider->box_open(buffer, blob_view{ sectorKeyNonce },
            buffer, contentMAC); decrx.has_failure())
        {
            return std::move(decrx).assume_error()
                << ed::sector_idx{ sectorIdx };
        }
        return outcome::success();
    }

    result<void> detail::raw_archive::write_sector(blob ciphertextBuffer, blob mac,
        basic_archive_file_meta &file, sector_id sectorIdx, blob_view data)
    {
        constexpr auto sectorIdxLimit = std::numeric_limits<std::uint64_t>::max() / sector_size;
        if (sectorIdx == sector_id::master)
        {
            return errc::invalid_argument;
        }
        if (static_cast<std::uint64_t>(sectorIdx) >= sectorIdxLimit)
        {
            return errc::invalid_argument;
        }
        if (data.size() != raw_archive::sector_payload_size)
        {
            return errc::invalid_argument;
        }
        if (ciphertextBuffer.size() != raw_archive::sector_payload_size)
        {
            return errc::invalid_argument;
        }

        std::array<std::byte, 32> salt;
        auto nonce = file.write_counter.fetch_increment();
        OUTCOME_TRY(crypto::kdf(blob{ salt }, nonce.view(), sector_kdf_salt, mSessionSalt));

        secure_byte_array<44> sectorKeyNonce;
        OUTCOME_TRY(crypto::kdf(blob{ sectorKeyNonce }, file.secret_view(), sector_kdf_prk, salt));

        VEFS_TRY_INJECT(mCryptoProvider->box_seal(ciphertextBuffer, mac,
            blob_view{ sectorKeyNonce }, data),
            ed::sector_idx{ sectorIdx }
        );

        const auto sectorOffset = to_offset(sectorIdx);
        std::error_code scode;
        mArchiveFile->write(blob_view{ salt }, sectorOffset, scode);
        if (scode)
        {
            return error{ scode }
                << ed::sector_idx{ sectorIdx };
        }
        mArchiveFile->write(ciphertextBuffer, sectorOffset + salt.size(), scode);
        if (scode)
        {
            return error{ scode }
                << ed::sector_idx{ sectorIdx };
        }

        return outcome::success();
    }

    result<void> vefs::detail::raw_archive::erase_sector(basic_archive_file_meta &file,
        sector_id sectorIdx)
    {
        if (sectorIdx == sector_id::master)
        {
            return errc::invalid_argument;
        }
        std::array<std::byte, 32> saltBuffer;
        blob salt{ saltBuffer };

        auto nonce = file.write_counter.fetch_increment();
        OUTCOME_TRY(crypto::kdf(salt, nonce.view(), sector_kdf_erase, mSessionSalt));

        const auto offset = to_offset(sectorIdx);
        std::error_code scode;
        mArchiveFile->write(salt, offset, scode);
        if (scode)
        {
            return error{ scode };
        }
        return outcome::success();
    }

    result<void> vefs::detail::raw_archive::update_header()
    {
        using adesso::vefs::ArchiveHeader;

        ArchiveHeader headerMsg;
        VEFS_SCOPE_EXIT{ erase_secrets(headerMsg); };
        headerMsg.set_allocated_archiveindex(pack(*mArchiveIdx));
        headerMsg.set_allocated_freeblockindex(pack(*mFreeBlockIdx));

        auto secretCtr = mArchiveSecretCounter.fetch_increment().value();
        auto journalCtr = mJournalCounter.load().value();

        auto nextSecretCtr = mArchiveSecretCounter.fetch_increment().value();
        headerMsg.set_archivesecretcounter(nextSecretCtr.data(), nextSecretCtr.size());
        headerMsg.set_journalcounter(journalCtr.data(), journalCtr.size());


        switch_header();
        const auto headerOffset = header_offset(mHeaderSelector);
        const auto fullHeaderSize = header_size(mHeaderSelector);

        secure_vector<std::byte> headerMem{ fullHeaderSize, std::byte{} };

        auto prefix = reinterpret_cast<ArchiveHeaderPrefix *>(headerMem.data());

        prefix->header_length = static_cast<std::uint32_t>(headerMsg.ByteSizeLong());
        if (!serialize_to_blob(blob{ headerMem }.slice(sizeof(ArchiveHeaderPrefix), prefix->header_length), headerMsg))
        {
            return archive_errc::protobuf_serialization_failed;
        }

        OUTCOME_TRY(crypto::kdf(blob{ prefix->header_salt }, blob_view{ secretCtr },
            archive_header_kdf_salt, mSessionSalt));

        secure_byte_array<44> headerKeyNonce;
        OUTCOME_TRY(crypto::kdf(blob{ headerKeyNonce }, master_secret_view(),
            archive_header_kdf_prk, prefix->header_salt));

        blob encryptedHeader = blob{ headerMem }.slice(prefix->unencrypted_prefix_size);
        VEFS_TRY_INJECT(mCryptoProvider->box_seal(encryptedHeader, blob{ prefix->header_mac },
            blob_view{ headerKeyNonce }, encryptedHeader),
            ed::archive_file{ "[archive-header]" }
        );

        std::error_code scode;
        mArchiveFile->write(blob_view{ headerMem }, headerOffset, scode);
        if (scode)
        {
            return error{ scode }
                << ed::archive_file{ "[archive-header]" };
        }
        return outcome::success();
    }

    result<void> vefs::detail::raw_archive::update_static_header(blob_view newUserPRK)
    {
        OUTCOME_TRY(write_static_archive_header(newUserPRK));

        // we only need to update one of the two headers as the format is robust enough to
        // deal with the probably corrupt other header
        return update_header();
    }
}
