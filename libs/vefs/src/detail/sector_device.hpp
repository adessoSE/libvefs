#pragma once

#include <array>
#include <memory>

#include <vefs/disappointment.hpp>
#include <vefs/platform/filesystem.hpp>
#include <vefs/span.hpp>
#include <vefs/utils/secure_array.hpp>

#include "../crypto/counter.hpp"
#include "../crypto/provider.hpp"
#include "file_crypto_ctx.hpp"
#include "root_sector_info.hpp"
#include "sector_id.hpp"

namespace adesso::vefs
{
    class ArchiveHeader;
}

namespace vefs::detail
{
    struct basic_archive_file_meta;

    class sector_device
    {
        enum class header_id
        {
            first = 0,
            second = 1,
        };

    public:
        enum class create_tag
        {
        };

        static constexpr size_t sector_size = 1 << 15;                        // 2^15
        static constexpr size_t sector_payload_size = sector_size - (1 << 5); // 2^15-2^5

        static constexpr auto to_offset(sector_id id) -> std::uint64_t;

        static auto open(filesystem::ptr fs, const std::filesystem::path &path,
                         crypto::crypto_provider *cryptoProvider, ro_blob<32> userPRK,
                         file_open_mode_bitset openMode) -> result<std::unique_ptr<sector_device>>;

        sector_device(file::ptr archiveFile, crypto::crypto_provider *cryptoProvider,
                    ro_blob<64> userPRK);
        sector_device(file::ptr archiveFile, crypto::crypto_provider *cryptoProvider,
                    ro_blob<64> userPRK, create_tag);
        ~sector_device() = default;

        auto read_sector(rw_blob<sector_payload_size> contentDest, const file_crypto_ctx &fileCtx,
                         sector_id sectorIdx, ro_blob<16> contentMAC) noexcept -> result<void>;
        auto write_sector(rw_blob<16> mac, file_crypto_ctx &fileCtx, sector_id sectorIdx,
                          ro_blob<sector_payload_size> data) noexcept -> result<void>;

        result<void> read_sector(rw_blob<sector_payload_size> buffer,
                                 const basic_archive_file_meta &file, sector_id sectorIdx,
                                 ro_dynblob contentMAC);
        result<void> write_sector(rw_blob<sector_payload_size> ciphertextBuffer, rw_dynblob mac,
                                  basic_archive_file_meta &file, sector_id sectorIdx,
                                  ro_blob<sector_payload_size> data);
        result<void> erase_sector(basic_archive_file_meta &file, sector_id sectorIdx);

        result<void> update_header();
        result<void> update_static_header(ro_blob<32> newUserPRK);

        // numSectors = number of sectors (i.e. including the master sector)
        result<void> resize(std::uint64_t numSectors);
        std::uint64_t size() const;
        result<void> sync();

        basic_archive_file_meta &index_file();
        basic_archive_file_meta &free_sector_index_file();

        ro_blob<64> master_secret_view() const;
        ro_blob<16> session_salt_view() const;

        const crypto::crypto_provider *crypto() const;

        crypto::atomic_counter &master_secret_counter();
        crypto::atomic_counter &journal_counter();

        auto create_file() noexcept -> result<basic_archive_file_meta>;

    private:
        sector_device(file::ptr archiveFile, crypto::crypto_provider *cryptoProvider);

        result<void> parse_static_archive_header(ro_blob<32> userPRK);
        result<void> parse_archive_header();
        result<void> parse_archive_header(std::size_t position, std::size_t size,
                                          adesso::vefs::ArchiveHeader &out);

        result<void> write_static_archive_header(ro_blob<32> userPRK);

        result<void> initialize_file(basic_archive_file_meta &file);

        auto header_size(header_id which) const noexcept;
        auto header_offset(header_id which) const noexcept;
        void switch_header() noexcept;

        crypto::crypto_provider *const mCryptoProvider;
        file::ptr mArchiveFile;

        std::unique_ptr<basic_archive_file_meta> mFreeBlockIdx;
        std::unique_ptr<basic_archive_file_meta> mArchiveIdx;

        utils::secure_byte_array<64> mArchiveMasterSecret;
        utils::secure_byte_array<16> mStaticHeaderWriteCounter;
        utils::secure_byte_array<16> mSessionSalt;
        crypto::atomic_counter mArchiveSecretCounter;
        crypto::atomic_counter mJournalCounter;

        std::atomic<uint64_t> mNumSectors;

        size_t mArchiveHeaderOffset;
        header_id mHeaderSelector;
    };
    static_assert(!std::is_default_constructible_v<sector_device>);
    static_assert(!std::is_copy_constructible_v<sector_device>);
    static_assert(!std::is_copy_assignable_v<sector_device>);
    static_assert(!std::is_move_constructible_v<sector_device>);
    static_assert(!std::is_move_assignable_v<sector_device>);

    constexpr std::uint64_t sector_device::to_offset(sector_id id)
    {
        return static_cast<std::uint64_t>(id) * sector_size;
    }

    inline auto vefs::detail::sector_device::resize(std::uint64_t numSectors) -> result<void>
    {
        std::error_code scode;
        mArchiveFile->resize(numSectors * sector_size, scode);
        if (scode)
        {
            return error{scode};
        }
        mNumSectors = numSectors;
        return outcome::success();
    }
    inline std::uint64_t sector_device::size() const
    {
        return mNumSectors.load();
    }

    inline basic_archive_file_meta &sector_device::index_file()
    {
        return *mArchiveIdx;
    }

    inline basic_archive_file_meta &sector_device::free_sector_index_file()
    {
        return *mFreeBlockIdx;
    }

    inline ro_blob<64> sector_device::master_secret_view() const
    {
        return as_span(mArchiveMasterSecret);
    }

    inline ro_blob<16> sector_device::session_salt_view() const
    {
        return as_span(mSessionSalt);
    }

    inline const crypto::crypto_provider *sector_device::crypto() const
    {
        return mCryptoProvider;
    }

    inline crypto::atomic_counter &sector_device::master_secret_counter()
    {
        return mArchiveSecretCounter;
    }

    inline crypto::atomic_counter &sector_device::journal_counter()
    {
        return mJournalCounter;
    }

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(push)
#pragma warning(disable : 4146) // unary minus operator on unsigned type in header_offset
#endif

    inline auto vefs::detail::sector_device::header_size(header_id which) const noexcept
    {
        return (sector_size - mArchiveHeaderOffset) / 2 +
               (static_cast<size_t>(which) & mArchiveHeaderOffset);
    }
    inline auto sector_device::header_offset(header_id which) const noexcept
    {
        return mArchiveHeaderOffset +
               ((-static_cast<std::size_t>(which)) & header_size(header_id::first));
    }
    inline void sector_device::switch_header() noexcept
    {
        mHeaderSelector =
            header_id{!static_cast<std::underlying_type_t<header_id>>(mHeaderSelector)};
    }

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(pop)
#endif
} // namespace vefs::detail
