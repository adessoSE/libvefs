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
        enum class header_id
        {
            first = 0,
            second = 1,
        };

    public:
        enum class create_tag {};

        static constexpr size_t sector_size = 1 << 15;
        static constexpr size_t sector_payload_size = sector_size - (1 << 5);

        std::uint64_t to_offset(sector_id id);

        raw_archive(file::ptr archiveFile, crypto::crypto_provider *cryptoProvider, blob_view userPRK);
        raw_archive(file::ptr archiveFile, crypto::crypto_provider *cryptoProvider, blob_view userPRK,
            create_tag);
        ~raw_archive();

        void read_sector(blob buffer, const raw_archive_file &file, sector_id sectorIdx,
                         blob_view contentMAC);
        void write_sector(blob ciphertextBuffer, blob mac, raw_archive_file &file,
                          sector_id sectorIdx, blob_view data);
        void erase_sector(raw_archive_file &file, sector_id sectorIdx);

        void update_header();
        void update_static_header(blob_view newUserPRK);

        // numSectors = number of sectors (i.e. including the master sector)
        void resize(std::uint64_t numSectors);
        std::uint64_t size() const;
        void sync();

        raw_archive_file & index_file();
        raw_archive_file & free_sector_index_file();

        blob_view master_secret_view() const;
        blob_view session_salt_view() const;

        const crypto::crypto_provider * crypto() const;

        crypto::atomic_counter & master_secret_counter();
        crypto::atomic_counter & journal_counter();

        std::shared_ptr<raw_archive_file> create_file();

    private:
        raw_archive(file::ptr archiveFile, crypto::crypto_provider *cryptoProvider);

        void parse_static_archive_header(blob_view userPRK);
        void parse_archive_header();
        auto parse_archive_header(std::size_t position, std::size_t size);

        void write_static_archive_header(blob_view userPRK);

        void initialize_file(raw_archive_file &file);

        void encrypt_sector(blob saltBuffer, blob ciphertextBuffer, blob mac, blob_view data,
                            blob_view fileKey, crypto::atomic_counter & nonceCtr);

        void write_sector_data(sector_id sectorIdx, blob_view sectorSalt, blob_view ciphertextBuffer);

        auto header_size(header_id which) const noexcept;
        auto header_offset(header_id which) const noexcept;
        void switch_header() noexcept;

        crypto::crypto_provider * const mCryptoProvider;
        file::ptr mArchiveFile;

        std::unique_ptr<raw_archive_file> mFreeBlockIdx;
        std::unique_ptr<raw_archive_file> mArchiveIdx;

        utils::secure_byte_array<64> mArchiveMasterSecret;
        utils::secure_byte_array<16> mStaticHeaderWriteCounter;
        utils::secure_byte_array<16> mSessionSalt;
        crypto::atomic_counter mArchiveSecretCounter;
        crypto::atomic_counter mJournalCounter;

        std::atomic<uint64_t> mNumSectors;

        size_t mArchiveHeaderOffset;
        header_id mHeaderSelector;
    };

    inline std::uint64_t raw_archive::to_offset(sector_id id)
    {
        return static_cast<std::uint64_t>(id) << 15;
    }

    inline void vefs::detail::raw_archive::resize(std::uint64_t numSectors)
    {
        mArchiveFile->resize(numSectors * sector_size);
        mNumSectors = numSectors;
    }
    inline std::uint64_t raw_archive::size() const
    {
        return mNumSectors.load();
    }

    inline raw_archive_file & raw_archive::index_file()
    {
        return *mArchiveIdx;
    }

    inline raw_archive_file & raw_archive::free_sector_index_file()
    {
        return *mFreeBlockIdx;
    }

    inline blob_view raw_archive::master_secret_view() const
    {
        return blob_view{ mArchiveMasterSecret };
    }

    inline blob_view raw_archive::session_salt_view() const
    {
        return blob_view{ mSessionSalt };
    }

    inline const crypto::crypto_provider * raw_archive::crypto() const
    {
        return mCryptoProvider;
    }

    inline crypto::atomic_counter & raw_archive::master_secret_counter()
    {
        return mArchiveSecretCounter;
    }

    inline crypto::atomic_counter & raw_archive::journal_counter()
    {
        return mJournalCounter;
    }

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(push)
#pragma warning(disable: 4146) // unary minus operator on unsigned type in header_offset
#endif

    inline auto vefs::detail::raw_archive::header_size(header_id which) const noexcept
    {
        return (sector_size - mArchiveHeaderOffset) / 2
            + (static_cast<size_t>(which) & mArchiveHeaderOffset);
    }
    inline auto raw_archive::header_offset(header_id which) const noexcept
    {
        return mArchiveHeaderOffset
            + ((-static_cast<std::size_t>(which)) & header_size(header_id::first));
    }
    inline void raw_archive::switch_header() noexcept
    {
        mHeaderSelector = header_id{
            !static_cast<std::underlying_type_t<header_id>>(mHeaderSelector)
        };
    }

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(pop)
#endif
}
