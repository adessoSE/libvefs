#pragma once

#include <array>
#include <memory>
#include <string_view>

#include <vefs/blob.hpp>
#include <vefs/filesystem.hpp>

#include <vefs/utils/secure_array.hpp>
#include <vefs/crypto/provider.hpp>
#include <vefs/crypto/counter.hpp>

#include <vefs/detail/sector_id.hpp>

namespace vefs::detail
{
    struct raw_archive_file;

    class raw_archive
    {
    public:
        enum class create_tag {};

        static constexpr size_t sector_size = 1 << 15;
        static constexpr size_t sector_payload_size = sector_size - (1 << 5);

        std::uint64_t to_offset(sector_id id)
        {
            return static_cast<std::uint64_t>(id) << 15;
        }

        raw_archive(file::ptr archiveFile, crypto::crypto_provider *cryptoProvider, blob_view userPRK);
        raw_archive(file::ptr archiveFile, crypto::crypto_provider *cryptoProvider, blob_view userPRK,
            create_tag);
        ~raw_archive();

        void read_sector(blob buffer, const raw_archive_file & file, sector_id sectorIdx,
                         blob_view contentMAC);
        void write_sector(blob ciphertextBuffer, blob mac, raw_archive_file & file,
                          sector_id sectorIdx, blob_view data);

        void update_header();
        void update_static_header(blob_view newUserPRK);


        raw_archive_file & index_file()
        {
            return *mArchiveIdx;
        }
        raw_archive_file & free_sector_index_file()
        {
            return *mFreeBlockIdx;
        }

        blob_view master_secret_view() const
        {
            return blob_view{ mArchiveMasterSecret };
        }
        blob_view session_salt_view() const
        {
            return blob_view{ mSessionSalt };
        }

        const crypto::crypto_provider * crypto() const
        {
            return mCryptoProvider;
        }

        crypto::atomic_counter & master_secret_counter()
        {
            return mArchiveSecretCounter;
        }
        crypto::atomic_counter & journal_counter()
        {
            return mJournalCounter;
        }


    private:
        raw_archive(file::ptr archiveFile, crypto::crypto_provider *cryptoProvider);

        void parse_static_archive_header(blob_view userPRK);
        void parse_archive_header();
        auto parse_archive_header(std::size_t position, std::size_t size);

        void write_static_archive_header(blob_view userPRK);

        void encrypt_sector(blob saltBuffer, blob ciphertextBuffer, blob mac, blob_view data,
                            blob_view fileKey, crypto::atomic_counter & nonceCtr);

        void write_sector_data(sector_id sectorIdx, blob_view sectorSalt, blob_view ciphertextBuffer);

        auto header_size(int which) const noexcept
        {
            return (sector_size - mArchiveHeaderOffset) / 2
                + which * (mArchiveHeaderOffset % 2);
        }

        crypto::crypto_provider * const mCryptoProvider;
        file::ptr mArchiveFile;

        std::unique_ptr<raw_archive_file> mFreeBlockIdx;
        std::unique_ptr<raw_archive_file> mArchiveIdx;

        utils::secure_byte_array<64> mArchiveMasterSecret;
        utils::secure_byte_array<16> mStaticHeaderWriteCounter;
        utils::secure_byte_array<16> mSessionSalt;
        crypto::atomic_counter mArchiveSecretCounter;
        crypto::atomic_counter mJournalCounter;

        size_t mArchiveHeaderOffset;
        unsigned int mHeaderSelector;
    };
}
