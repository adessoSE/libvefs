#pragma once

#include <string_view>

#include <vefs/blob.hpp>
#include <vefs/filesystem.hpp>

#include <vefs/utils/secure_array.hpp>
#include <vefs/crypto/provider.hpp>
#include <vefs/crypto/counter.hpp>

namespace vefs::detail
{
    class raw_archive
    {
    public:
        static constexpr size_t sector_size = 1 << 15;
        static constexpr size_t sector_payload_size = sector_size - (1 << 5);

        raw_archive(filesystem::ptr fs, std::string_view path, crypto::crypto_provider *cryptoProvider,
            blob_view userPRK);

    private:
        void process_existing_journal(std::string_view jnlPath);
        void parse_static_archive_header(blob_view userPRK);
        void parse_archive_header();

        void read_sector(blob buffer, std::uint64_t sectorIdx, blob_view contentMAC,
            blob_view fileKey);

        void write_sector(blob_view data, std::uint64_t sectorIdx, blob_view fileKey,
            crypto::counter &nonce);

        blob_view master_secret_view() const
        {
            return blob_view{ mArchiveMasterSecret };
        }

        crypto::crypto_provider * const mCryptoProvider;
        const filesystem::ptr mParentFs;
        file::ptr mArchiveFile;
        file::ptr mJournalFile;
        const std::string mArchivePath;

        utils::secure_byte_array<64> mArchiveMasterSecret;
        utils::secure_byte_array<16> mSessionSalt;

        size_t mArchiveHeaderOffset;
    };


}
