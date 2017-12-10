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
#include <vefs/detail/archive_file.hpp>

#include <boost/multiprecision/cpp_int.hpp>

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

        inline utils::xoroshiro128plus create_engine()
        {
            std::uint64_t randomState[2];
            random_bytes(blob{ reinterpret_cast<std::byte *>(randomState), sizeof(randomState) });

            return xoroshiro128plus{ randomState[0], randomState[1] };
        }
    }

    void vefs::detail::raw_archive::initialize_file(raw_archive_file &file)
    {
        auto ctrValue = mArchiveSecretCounter.fetch_increment().value();
        blob_view ctrValueView{ ctrValue };
        crypto::kdf(master_secret_view(), { file_kdf_secret, ctrValueView, session_salt_view() },
            blob{ file.secret });

        {
            crypto::counter::state fileWriteCtrState;
            ctrValue = mArchiveSecretCounter.fetch_increment().value();
            crypto::kdf(master_secret_view(), { file_kdf_counter, ctrValueView },
                blob{ fileWriteCtrState });
            file.write_counter.store(crypto::counter{ fileWriteCtrState });
        }

        file.start_block_idx = sector_id{};
        file.start_block_mac = {};
        file.size = 0;
        file.tree_depth = -1;
    }

    std::shared_ptr<raw_archive_file> raw_archive::create_file()
    {
        //TODO: improve id generation
        thread_local vefs::utils::xoroshiro128plus engine = create_engine();
        thread_local boost::uuids::basic_random_generator<decltype(engine)> generate_id{ engine };

        auto file = std::make_shared<raw_archive_file>();

        file->id = file_id{ generate_id() };

        initialize_file(*file);

        return file;
    }

    raw_archive::raw_archive(file::ptr archiveFile, crypto::crypto_provider * cryptoProvider)
        : mCryptoProvider(cryptoProvider)
        , mArchiveFile(std::move(archiveFile))
        , mSessionSalt(cryptoProvider->generate_session_salt())
        , mNumSectors(0)
    {
        mNumSectors = mArchiveFile->size() / sector_size;
    }

    raw_archive::raw_archive(file::ptr archiveFile, crypto::crypto_provider *cryptoProvider,
            blob_view userPRK)
        : raw_archive(archiveFile, cryptoProvider)
    {
        if (mArchiveFile->size() < sector_size)
        {
            // at least the master sector is required
            BOOST_THROW_EXCEPTION(archive_corrupted{}
                << errinfo_archive_file{ "[master-sector]" });
        }

        try
        {
            parse_static_archive_header(userPRK);
        }
        catch (boost::exception &exc)
        {
            exc << errinfo_archive_file{ "[master-sector/static-header]" };
            throw;
        }
        try
        {
            parse_archive_header();
        }
        catch (boost::exception &exc)
        {
            exc << errinfo_archive_file{ "[master-sector/header]" };
            throw;
        }
    }

    raw_archive::raw_archive(file::ptr archiveFile, crypto::crypto_provider * cryptoProvider,
        blob_view userPRK, create_tag)
        : raw_archive(std::move(archiveFile), cryptoProvider)
    {
        // allocate the master sector
        resize(1);

        mCryptoProvider->random_bytes(blob{ mArchiveMasterSecret });
        mCryptoProvider->random_bytes(blob{ mStaticHeaderWriteCounter });

        write_static_archive_header(userPRK);
        mFreeBlockIdx = std::make_unique<raw_archive_file>();
        mFreeBlockIdx->id = file_id::free_block_index;
        initialize_file(*mFreeBlockIdx);
        mArchiveIdx = std::make_unique<raw_archive_file>();
        mArchiveIdx->id = file_id::archive_index;
        initialize_file(*mArchiveIdx);
    }

    raw_archive::~raw_archive()
    {
    }

    void raw_archive::parse_static_archive_header(blob_view userPRK)
    {
        StaticArchiveHeaderPrefix archivePrefix;
        mArchiveFile->read(as_blob(archivePrefix), 0);

        // check for magic number
        if (!equal(blob_view{ archivePrefix.magic_number }, archive_magic_number_view))
        {
            BOOST_THROW_EXCEPTION(archive_corrupted{});
        }
        // the static archive header must be within the bounds of the first block
        if (archivePrefix.static_header_length >= sector_size - sizeof(StaticArchiveHeaderPrefix))
        {
            BOOST_THROW_EXCEPTION(archive_corrupted{});
        }

        secure_vector<std::byte> staticHeaderMem{ archivePrefix.static_header_length, std::byte{} };
        blob staticHeader{ staticHeaderMem };
        try
        {
            mArchiveFile->read(staticHeader, sizeof(StaticArchiveHeaderPrefix));
        }
        catch (const std::system_error &)
        {
            throw;
        }

        auto boxOpened = mCryptoProvider->box_open(/* out */staticHeader, /* in */staticHeader,
            blob_view{ archivePrefix.static_header_mac },
            [userPRK, &archivePrefix](blob prkOut)
            {
                crypto::kdf(userPRK,
                    { archive_static_header_kdf_prk, blob_view{ archivePrefix.static_header_salt } },
                    prkOut);
            });

        if (!boxOpened)
        {
            BOOST_THROW_EXCEPTION(archive_corrupted{});
        }

        adesso::vefs::StaticArchiveHeader staticHeaderMsg;
        VEFS_SCOPE_EXIT { erase_secrets(staticHeaderMsg); };

        if (!parse_blob(staticHeaderMsg, staticHeader))
        {
            BOOST_THROW_EXCEPTION(archive_corrupted{});
        }
        if (staticHeaderMsg.formatversion() != 0)
        {
            BOOST_THROW_EXCEPTION(unknown_archive_version{});
        }
        if (staticHeaderMsg.mastersecret().size() != 64
            || staticHeaderMsg.staticarchiveheaderwritecounter().size() != 16)
        {
            BOOST_THROW_EXCEPTION(archive_corrupted{});
        }

        blob_view{ staticHeaderMsg.mastersecret() }.copy_to(blob{ mArchiveMasterSecret });
        blob_view{ staticHeaderMsg.staticarchiveheaderwritecounter() }
            .copy_to(blob{ mStaticHeaderWriteCounter });

        mArchiveHeaderOffset = sizeof(StaticArchiveHeaderPrefix) + archivePrefix.static_header_length;
    }

    auto raw_archive::parse_archive_header(std::size_t position, std::size_t size)
    {
        using adesso::vefs::ArchiveHeader;

        secure_vector<std::byte> headerAndPaddingMem(size, std::byte{});
        blob headerAndPadding{ headerAndPaddingMem };

        mArchiveFile->read(headerAndPadding, position);

        auto archiveHeaderPrefix = reinterpret_cast<ArchiveHeaderPrefix *>(headerAndPaddingMem.data());

        auto encryptedHeaderPart = headerAndPadding.slice(ArchiveHeaderPrefix::unencrypted_prefix_size);
        auto boxOpened = mCryptoProvider->box_open(encryptedHeaderPart, encryptedHeaderPart,
            blob_view{ archiveHeaderPrefix->header_mac },
            [this, &archiveHeaderPrefix](blob prkBuffer)
            {
                crypto::kdf(master_secret_view(),
                    { archive_header_kdf_prk, blob_view{ archiveHeaderPrefix->header_salt } },
                    prkBuffer);
            });

        if (!boxOpened)
        {
            return std::optional<ArchiveHeader>{};
        }

        auto headerMsg = std::make_optional<ArchiveHeader>();
        VEFS_ERROR_EXIT{ erase_secrets(*headerMsg); };

        if (!parse_blob(*headerMsg, headerAndPadding.slice(sizeof(ArchiveHeaderPrefix),
                        archiveHeaderPrefix->header_length)))
        {
            BOOST_THROW_EXCEPTION(archive_corrupted{});
        }

        // the archive is corrupted if the header message doesn't pass parameter validation
        // simple write failures and incomplete writes are catched by the AE construction
        if (headerMsg->archivesecretcounter().size() != 16
            || headerMsg->journalcounter().size() != 16
            || !headerMsg->has_archiveindex()
            || !headerMsg->has_freeblockindex())
        {
            BOOST_THROW_EXCEPTION(archive_corrupted{});
        }

        //TODO: further validation
        return headerMsg;
    }

    void raw_archive::parse_archive_header()
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


        const auto headerSize0 = header_size(0);

        auto first = parse_archive_header(mArchiveHeaderOffset, headerSize0);
        VEFS_SCOPE_EXIT{
            if (first)
            {
                erase_secrets(*first);
            }
        };

        auto second = parse_archive_header(mArchiveHeaderOffset + headerSize0, header_size(1));
        VEFS_SCOPE_EXIT{
            if (second)
            {
                erase_secrets(*second);
            }
        };

        // determine which header to apply
        if (first && second)
        {
            using boost::multiprecision::uint128_t;

            const auto toBigInt = [](const std::string &data)
            {
                uint128_t store;
                auto *begin = reinterpret_cast<const uint8_t *>(data.data());
                auto *end = begin + 16;

                import_bits(store, begin, end);
                return store;
            };

            auto firstCtr = toBigInt(first->archivesecretcounter());
            auto secondCtr = toBigInt(second->archivesecretcounter());

            // please note that this likely isn't a constant time comparison
            // so this can leak information about the archive secret counter
            // in an online context.
            // consider adapting constant time comparison, e.g. from there:
            // https://github.com/chmike/cst_time_memcmp
            auto cmp = firstCtr.compare(secondCtr);
            if (!cmp)
            {
                // both headers are at the same counter value which is an invalid
                // state which cannot be produced by a conforming implementation
                BOOST_THROW_EXCEPTION(archive_corrupted{});
            }

            if (0 < cmp)
            {
                mHeaderSelector = 0;
                apply(*first);
            }
            else
            {
                mHeaderSelector = 1;
                apply(*second);
            }
        }
        else if (first)
        {
            mHeaderSelector = 0;
            apply(*first);

        }
        else if (second)
        {
            mHeaderSelector = 1;
            apply(*second);
        }
        else
        {
            BOOST_THROW_EXCEPTION(archive_corrupted{});
        }
    }

    void raw_archive::write_static_archive_header(blob_view userPRK)
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

        blob_view writeCtrBlob{ mStaticHeaderWriteCounter };

        crypto::kdf(writeCtrBlob, { archive_static_header_kdf_salt, session_salt_view() },
            blob{ headerPrefix.static_header_salt });

        header.set_mastersecret(
            std::string{ reinterpret_cast<char *>(mArchiveMasterSecret.data()), mArchiveMasterSecret.size() }
        );

        headerPrefix.static_header_length = static_cast<uint32_t>(header.ByteSizeLong());
        secure_vector<std::byte> msgHolder{ headerPrefix.static_header_length, std::byte{} };
        blob msg{ msgHolder };

        if (!serialize_to_blob(msg, header))
        {
            BOOST_THROW_EXCEPTION(logic_error{});
        }

        mCryptoProvider->box_seal(msg, blob{ headerPrefix.static_header_mac }, msg,
            [userPRK, &headerPrefix](blob prk)
            {
                crypto::kdf(userPRK,
                    { archive_static_header_kdf_prk, blob_view{ headerPrefix.static_header_salt } },
                    prk);
            });

        mArchiveFile->write(as_blob_view(headerPrefix), 0);
        mArchiveFile->write(msg, sizeof(headerPrefix));

        mArchiveHeaderOffset = sizeof(headerPrefix) + headerPrefix.static_header_length;
    }

    void raw_archive::read_sector(blob buffer, const raw_archive_file & file,
        sector_id sectorIdx, blob_view contentMAC)
    {
        const auto sectorOffset = to_offset(sectorIdx);

        try
        {
            if (buffer.size() != raw_archive::sector_payload_size)
            {
                BOOST_THROW_EXCEPTION(std::invalid_argument{ "the read buffer size doesn't match the sector payload size" });
            }

            secure_byte_array<32> sectorSaltMem;
            blob sectorSalt{ sectorSaltMem };

            try
            {
                mArchiveFile->read(sectorSalt, sectorOffset);
                mArchiveFile->read(buffer, sectorOffset + sectorSalt.size());
            }
            catch (const std::system_error &)
            {
                throw;
            }

            auto boxOpened = mCryptoProvider->box_open(buffer, buffer, contentMAC,
                [&file, sectorSalt](blob prkOut)
                {
                    crypto::kdf(file.secret_view(),
                        { sector_kdf_prk, blob_view{ sectorSalt } },
                        prkOut);
                });

            if (!boxOpened)
            {
                BOOST_THROW_EXCEPTION(archive_corrupted{});
            }
        }
        catch (boost::exception &exc)
        {
            exc << errinfo_sector_idx{ sectorIdx };
        }
    }

    void vefs::detail::raw_archive::write_sector(blob ciphertextBuffer, blob mac,
        raw_archive_file &file, sector_id sectorIdx, blob_view data)
    {
        std::array<std::byte, 32> salt;

        encrypt_sector(blob{ salt }, ciphertextBuffer, mac, data, file.secret_view(), file.write_counter);
        write_sector_data(sectorIdx, blob{ salt }, ciphertextBuffer);
    }

    void vefs::detail::raw_archive::erase_sector(raw_archive_file &file, sector_id sectorIdx)
    {
        if (sectorIdx == sector_id::master)
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{ "cannot erase the first sector" });
        }
        std::array<std::byte, 32> saltBuffer;
        blob salt{ saltBuffer };

        auto nonce = file.write_counter.fetch_increment();
        crypto::kdf(nonce.view(), { sector_kdf_erase, blob_view{ mSessionSalt } }, salt);

        const auto offset = to_offset(sectorIdx);
        mArchiveFile->write(salt, offset);
    }

    void vefs::detail::raw_archive::update_header()
    {
        using adesso::vefs::ArchiveHeader;

        ArchiveHeader headerMsg;
        headerMsg.set_allocated_archiveindex(pack(*mArchiveIdx));
        headerMsg.set_allocated_freeblockindex(pack(*mFreeBlockIdx));

        auto secretCtr = mArchiveSecretCounter.fetch_increment().value();
        auto journalCtr = mJournalCounter.load().value();

        auto nextSecretCtr = mArchiveSecretCounter.fetch_increment().value();
        headerMsg.set_archivesecretcounter(nextSecretCtr.data(), nextSecretCtr.size());
        headerMsg.set_journalcounter(journalCtr.data(), journalCtr.size());


        mHeaderSelector = !mHeaderSelector;
        const auto headerOffset
            = mArchiveHeaderOffset + mHeaderSelector * header_size(0);
        const auto fullHeaderSize = header_size(mHeaderSelector);

        secure_vector<std::byte> headerMem{ fullHeaderSize, std::byte{} };

        auto prefix = reinterpret_cast<ArchiveHeaderPrefix *>(headerMem.data());

        prefix->header_length = static_cast<std::uint32_t>(headerMsg.ByteSizeLong());
        if (!serialize_to_blob(blob{ headerMem }.slice(sizeof(ArchiveHeaderPrefix), prefix->header_length), headerMsg))
        {
            BOOST_THROW_EXCEPTION(logic_error{});
        }

        crypto::kdf(blob_view{ secretCtr }, { archive_header_kdf_salt, blob_view{ mSessionSalt } },
            blob{ prefix->header_salt });

        blob encryptedHeader = blob{ headerMem }.slice(prefix->unencrypted_prefix_size);
        mCryptoProvider->box_seal(encryptedHeader, blob{prefix->header_mac}, encryptedHeader,
            [this, &prefix](blob prk)
            {
                crypto::kdf(master_secret_view(), { archive_header_kdf_prk, blob_view{ prefix->header_salt } }, prk);
            });

        mArchiveFile->write(blob_view{ headerMem }, headerOffset);
    }

    void vefs::detail::raw_archive::update_static_header(blob_view newUserPRK)
    {
        write_static_archive_header(newUserPRK);

        // we only need to update one of the two headers as the format is robust enough to
        // deal with the probably corrupt other header
        update_header();
    }

    void raw_archive::write_sector_data(sector_id sectorIdx, blob_view sectorSalt,
        blob_view ciphertextBuffer)
    {
        if (sectorSalt.size() != 1 << 5)
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{ "sectorSalt has an inappropriate size" });
        }
        if (ciphertextBuffer.size() != raw_archive::sector_payload_size)
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{ "ciphertextBuffer has an inappropriate size" });
        }
        if (sectorIdx == sector_id::master)
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{ "cannot write the first sector" });
        }
        if (static_cast<std::uint64_t>(sectorIdx) >= std::numeric_limits<std::uint64_t>::max() / sector_size)
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{ "sector id points to a sector which isn't within the supported archive file size" });
        }

        const auto sectorOffset = to_offset(sectorIdx);

        mArchiveFile->write(sectorSalt, sectorOffset);
        mArchiveFile->write(ciphertextBuffer, sectorOffset + sectorSalt.size());
    }

    void vefs::detail::raw_archive::encrypt_sector(blob saltBuffer, blob ciphertextBuffer, blob mac,
        blob_view data, blob_view fileKey, crypto::atomic_counter & nonceCtr)
    {
        if (data.size() != raw_archive::sector_payload_size)
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{ "data has an inappropriate size" });
        }
        if (ciphertextBuffer.size() != raw_archive::sector_payload_size)
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{ "ciphertextBuffer has an inappropriate size" });
        }

        auto nonce = nonceCtr.fetch_increment();
        crypto::kdf(nonce.view(), { sector_kdf_salt, blob_view{ mSessionSalt } }, saltBuffer);

        mCryptoProvider->box_seal(ciphertextBuffer, mac, data,
            [fileKey, saltBuffer](blob prk)
            {
                crypto::kdf(fileKey, { sector_kdf_prk, blob_view{ saltBuffer } }, prk);
            });
    }
}
