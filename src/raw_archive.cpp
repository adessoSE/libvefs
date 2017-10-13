#include "precompiled.hpp"
#include <vefs/detail/raw_archive.hpp>

#include <array>

#include <vefs/crypto/kdf.hpp>
#include <vefs/utils/misc.hpp>
#include <vefs/utils/secure_array.hpp>
#include <vefs/utils/secure_allocator.hpp>

#include "fileformat.pb.h"

using namespace vefs::utils;


namespace vefs::detail
{
    namespace
    {
        constexpr std::array<std::byte, 4> archive_magic_number = 0x76'65'66'73_as_bytes;
        constexpr blob_view archive_magic_number_view = blob_view{ archive_magic_number };

        const blob_view archive_static_header_kdf_prk = "vefs/prk/StaticArchiveHeaderPRK"_bv;
        const blob_view archive_header_kdf_prk = "vefs/prk/ArchiveHeaderPRK"_bv;

        const blob_view sector_kdf_salt = "vefs/salt/Sector-Salt"_bv;
        const blob_view sector_kdf_prk = "vefs/prk/SectorPRK"_bv;

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
        };
        static_assert(sizeof(ArchiveHeaderPrefix) == 52);

#pragma pack(pop)

        void erase_secrets(adesso::vefs::FileDescriptor &fd)
        {
            if (auto secretPtr = fd.mutable_filesecret())
            {
                secure_memzero(blob{ *secretPtr });
            }
        }

        void erase_secrets(adesso::vefs::ArchiveHeader &header)
        {
            if (auto indexPtr = header.mutable_archiveindex())
            {
                erase_secrets(*indexPtr);
            }
            if (auto freeSectorIndexPtr = header.mutable_freeblockindex())
            {
                erase_secrets(*freeSectorIndexPtr);
            }
        }
    }

    raw_archive::raw_archive(filesystem::ptr fs, std::string_view path,
            crypto::crypto_provider *cryptoProvider, blob_view userPRK)
        : mCryptoProvider(cryptoProvider)
        , mParentFs(std::move(fs))
        , mArchiveFile()
        , mJournalFile()
        , mArchivePath(path)
    {
        if (!mParentFs)
        {
            throw std::invalid_argument("fs == null");
        }
        if (mArchivePath.empty())
        {
            throw std::invalid_argument("empty archive path");
        }

        // open the actual archive
        mArchiveFile = mParentFs->open(mArchivePath, file_open_mode::read | file_open_mode::write);
        if (mArchiveFile->size() < sector_size)
        {
            // at least the master sector is required
            throw "";
        }

        parse_static_archive_header(userPRK);
        parse_archive_header();

        // after successfully opening the existing archive we may need to revive
        // some semi-dead content logged in an existing journal and create a new one
        // afterwards
        auto jnlPath = mArchivePath + ".jnl";
        process_existing_journal(jnlPath);
        mJournalFile = mParentFs->open(jnlPath, file_open_mode::read | file_open_mode::write | file_open_mode::create);
    }

    void raw_archive::process_existing_journal(std::string_view jnlPath)
    {
        std::error_code ec;
        auto journalFile = mParentFs->open(jnlPath, file_open_mode::read, ec);
        if (!ec)
        {
            //TODO: read existing archive journal
        }
    }

    void raw_archive::parse_static_archive_header(blob_view userPRK)
    {
        StaticArchiveHeaderPrefix archivePrefix;
        try
        {
            mArchiveFile->read(as_blob(archivePrefix), 0);
        }
        catch (const std::system_error &)
        {
            //TODO: may need to add additional information
            throw;
        }

        // check for magic number
        if (!equal(blob_view{ archivePrefix.magic_number }, archive_magic_number_view))
        {
            //TODO: throw proper exception
            throw "";
        }
        // the static archive header must be within the bounds of the first block
        if (archivePrefix.static_header_length >= sector_size - sizeof(StaticArchiveHeaderPrefix))
        {
            throw "";
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
            [&userPRK, &archivePrefix](blob prkOut)
            {
                crypto::kdf(userPRK,
                    { archive_static_header_kdf_prk, blob_view{ archivePrefix.static_header_salt } },
                    prkOut);
            });

        if (!boxOpened)
        {
            // failed to decrypt/tag mismatch
            throw "";
        }

        adesso::vefs::StaticArchiveHeader staticHeaderMsg;
        VEFS_SCOPE_EXIT {
            if (auto secretPtr = staticHeaderMsg.mutable_mastersecret())
            {
                secure_memzero(blob{ *secretPtr });
            }
        };

        if (!staticHeaderMsg.ParseFromArray(staticHeader.data(), static_cast<int>(staticHeader.size())))
        {
            // invalid protobuf message
            throw "";
        }
        if (staticHeaderMsg.formatversion() != 0)
        {
            // unknown format version (backwards incompatible format change)
            throw "";
        }
        if (staticHeaderMsg.mastersecret().size() != 64
            || staticHeaderMsg.staticarchiveheaderwritecounter().size() != 16)
        {
            // required information is missing
            throw "";
        }

        blob_view{ staticHeaderMsg.mastersecret() }.copy_to(blob{ mArchiveMasterSecret });

        mArchiveHeaderOffset = sizeof(StaticArchiveHeaderPrefix) + archivePrefix.static_header_length;
    }

    void raw_archive::parse_archive_header()
    {
        constexpr auto unencryptedPrefixSize
            = sizeof(ArchiveHeaderPrefix::header_salt)
            + sizeof(ArchiveHeaderPrefix::header_mac);


        auto headerPlusPaddingSize = sector_size - mArchiveHeaderOffset;
        if (headerPlusPaddingSize < sizeof(ArchiveHeaderPrefix) + 1)
        {
            throw "";
        }

        secure_vector<std::byte> headerAndPaddingMem(headerPlusPaddingSize, std::byte{});
        blob headerAndPadding{ headerAndPaddingMem };

        try
        {
            mArchiveFile->read(headerAndPadding, mArchiveHeaderOffset);
        }
        catch (const std::system_error &)
        {
            // ...
            throw;
        }

        auto archiveHeaderPrefix = reinterpret_cast<ArchiveHeaderPrefix *>(headerAndPaddingMem.data());

        auto encryptedHeaderPart = headerAndPadding.slice(unencryptedPrefixSize);
        auto boxOpened = mCryptoProvider->box_open(encryptedHeaderPart, encryptedHeaderPart,
            blob_view{ archiveHeaderPrefix->header_mac },
            [this, &archiveHeaderPrefix](blob prkBuffer)
            {
                crypto::kdf(master_secret_view(),
                    { archive_header_kdf_prk, blob_view{ archiveHeaderPrefix->header_salt } },
                    prkBuffer);
            });

        adesso::vefs::ArchiveHeader headerMsg;
        VEFS_SCOPE_EXIT{ erase_secrets(headerMsg); };

        if (!headerMsg.ParseFromArray(headerAndPadding.slice(sizeof(ArchiveHeaderPrefix)).data(),
            archiveHeaderPrefix->header_length))
        {
            throw "";
        }



    }

    void raw_archive::read_sector(blob buffer, std::uint64_t sectorIdx, blob_view contentMAC,
        blob_view fileKey)
    {
        const auto sectorOffset = sectorIdx * raw_archive::sector_size;

        if (buffer.size() != raw_archive::sector_payload_size)
        {
            throw "";
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
            [fileKey, sectorSalt](blob prkOut)
            {
                crypto::kdf(fileKey,
                    { sector_kdf_prk, blob_view{ sectorSalt } },
                    prkOut);
            });

        if (!boxOpened)
        {
            throw "";
        }
    }

    void raw_archive::write_sector(blob_view data, std::uint64_t sectorIdx,
        blob_view fileKey, crypto::counter& nonceCtr)
    {
        const auto sectorOffset = sectorIdx * raw_archive::sector_size;

        if (data.size() != raw_archive::sector_payload_size)
        {
            throw "";
        }

        secure_byte_array<32> sectorSaltMem;
        blob sectorSalt{ sectorSaltMem };

        auto nonce = nonceCtr.fetch_increment();
        crypto::kdf(blob_view{ nonce }, { sector_kdf_salt, blob_view{ mSessionSalt } }, sectorSalt );

        mCryptoProvider->box_seal()


    }
}
