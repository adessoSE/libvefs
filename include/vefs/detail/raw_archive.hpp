#pragma once

#include <array>
#include <memory>
#include <string_view>

#include <vefs/blob.hpp>
#include <vefs/filesystem.hpp>

#include <vefs/utils/secure_array.hpp>
#include <vefs/crypto/provider.hpp>
#include <vefs/crypto/counter.hpp>

namespace vefs::detail
{
    struct raw_archive_file;

    class raw_archive
    {
    public:
        enum class create_tag {};

        static constexpr size_t sector_size = 1 << 15;
        static constexpr size_t sector_payload_size = sector_size - (1 << 5);

        raw_archive(filesystem::ptr fs, std::string_view path, crypto::crypto_provider *cryptoProvider,
            blob_view userPRK);
        raw_archive(filesystem::ptr fs, std::string_view path, crypto::crypto_provider *cryptoProvider,
            blob_view userPRK, create_tag);
        ~raw_archive();

        void read_sector(blob buffer, const raw_archive_file & file, std::uint64_t sectorIdx,
                         blob_view contentMAC);
        void write_sector(blob ciphertextBuffer, blob mac, raw_archive_file & file,
                          std::uint64_t sectorIdx, blob_view data);

        void update_header();
        void update_static_header(blob_view newUserPRK);


        raw_archive_file & index_file()
        {
            return *mArchiveIdx;
        }
        raw_archive_file & raw_block_index_file()
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

    private:
        raw_archive(filesystem::ptr fs, std::string_view path, crypto::crypto_provider *cryptoProvider);

        void process_existing_journal(std::string_view jnlPath);
        void parse_static_archive_header(blob_view userPRK);
        void parse_archive_header();

        void write_static_archive_header(blob_view userPRK);

        void encrypt_sector(blob saltBuffer, blob ciphertextBuffer, blob mac, blob_view data,
                            blob_view fileKey, crypto::counter & nonceCtr);

        void write_sector_data(std::uint64_t sectorIdx, blob_view sectorSalt, blob_view ciphertextBuffer);

        auto header_size(int which) const noexcept
        {
            return (sector_size - mArchiveHeaderOffset) / 2
                + which * (mArchiveHeaderOffset % 2);
        }

        crypto::crypto_provider * const mCryptoProvider;
        const filesystem::ptr mParentFs;
        file::ptr mArchiveFile;
        file::ptr mJournalFile;
        const std::string mArchivePath;

        std::unique_ptr<raw_archive_file> mFreeBlockIdx;
        std::unique_ptr<raw_archive_file> mArchiveIdx;

        utils::secure_byte_array<64> mArchiveMasterSecret;
        utils::secure_byte_array<16> mStaticHeaderWriteCounter;
        utils::secure_byte_array<16> mJournalCounter;
        utils::secure_byte_array<16> mSessionSalt;
        crypto::counter mArchiveSecretCounter;

        size_t mArchiveHeaderOffset;
        unsigned int mHeaderSelector;
    };

    struct raw_archive_file
    {
        blob_view secret_view() const
        {
            return blob_view{ secret };
        }

        blob start_block_mac_blob()
        {
            return blob{ start_block_mac };
        }

        utils::secure_byte_array<32> secret;
        crypto::counter write_counter;
        std::array<std::byte, 16> start_block_mac;

        std::string id;

        std::uint64_t start_block_idx;
        std::uint64_t size;
        int tree_depth;
    };
}
