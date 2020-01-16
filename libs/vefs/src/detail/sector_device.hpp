#pragma once

#include <array>
#include <memory>
#include <type_traits>

#include <vefs/llfio.hpp>

#include <vefs/disappointment.hpp>
#include <vefs/disappointment/llfio_adapter.hpp>
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
    class FileDescriptor;
} // namespace adesso::vefs

namespace vefs::detail
{
    struct master_file_info
    {
        file_crypto_ctx crypto_ctx;
        root_sector_info tree_info;
    };

    struct archive_header_content
    {
        master_file_info filesystem_index;
        master_file_info free_sector_index;
    };

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

        static constexpr size_t sector_size = 1 << 15; // 2^15
        static constexpr size_t sector_payload_size =
            sector_size - (1 << 5); // 2^15-2^5

        static constexpr auto to_offset(sector_id id) -> std::uint64_t;

        static auto open(llfio::mapped_file_handle mfh,
                         crypto::crypto_provider *cryptoProvider,
                         ro_blob<32> userPRK, bool createNew)
            -> result<std::unique_ptr<sector_device>>;

        ~sector_device() = default;

        auto read_sector(rw_blob<sector_payload_size> contentDest,
                         const file_crypto_ctx &fileCtx, sector_id sectorIdx,
                         ro_blob<16> contentMAC) noexcept -> result<void>;
        auto write_sector(rw_blob<16> mac, file_crypto_ctx &fileCtx,
                          sector_id sectorIdx,
                          ro_blob<sector_payload_size> data) noexcept
            -> result<void>;
        auto erase_sector(sector_id sectorIdx) noexcept -> result<void>;

        result<void> update_header();
        result<void> update_static_header(ro_blob<32> newUserPRK);

        // numSectors = number of sectors (i.e. including the master sector)
        result<void> resize(std::uint64_t numSectors);
        std::uint64_t size() const;

        auto archive_header() noexcept -> archive_header_content &;

        ro_blob<64> master_secret_view() const;
        ro_blob<16> session_salt_view() const;

        const crypto::crypto_provider *crypto() const;
        crypto::atomic_counter &master_secret_counter();

        auto create_file_secrets() noexcept
            -> result<std::unique_ptr<file_crypto_ctx>>;

    private:
        sector_device(llfio::mapped_file_handle mfh,
                      crypto::crypto_provider *cryptoProvider,
                      size_t numSectors);

        result<void> parse_static_archive_header(ro_blob<32> userPRK);
        result<void> parse_archive_header();
        result<void> parse_archive_header(std::size_t position,
                                          std::size_t size,
                                          adesso::vefs::ArchiveHeader &out);

        result<void> write_static_archive_header(ro_blob<32> userPRK);

        static constexpr auto header_size(header_id which) noexcept
            -> std::size_t;
        auto header_offset(header_id which) const noexcept -> std::size_t;
        void switch_header() noexcept;

        crypto::crypto_provider *const mCryptoProvider;
        llfio::mapped_file_handle mArchiveFile;

        archive_header_content mHeaderContent;

        utils::secure_byte_array<64> mArchiveMasterSecret;
        utils::secure_byte_array<16> mStaticHeaderWriteCounter;
        utils::secure_byte_array<16> mSessionSalt;
        crypto::atomic_counter mArchiveSecretCounter;
        crypto::atomic_counter mJournalCounter;
        std::atomic<std::uint64_t> mEraseCounter;

        std::atomic<uint64_t> mNumSectors;

        static inline constexpr size_t mArchiveHeaderOffset = 1 << 13;
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

    inline auto vefs::detail::sector_device::resize(std::uint64_t numSectors)
        -> result<void>
    {
        VEFS_TRY(bytesTruncated,
                 mArchiveFile.truncate(numSectors * sector_size));
        if (bytesTruncated != (numSectors * sector_size))
        {
            return outcome::failure(errc::bad);
        }
        mNumSectors = numSectors;

        return outcome::success();
    }
    inline std::uint64_t sector_device::size() const
    {
        return mNumSectors.load();
    }

    inline auto sector_device::archive_header() noexcept
        -> archive_header_content &
    {
        return mHeaderContent;
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

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(push)
// C4146: unary minus operator on unsigned type in header_offset
#pragma warning(disable : 4146)
#endif

    constexpr auto
    vefs::detail::sector_device::header_size(header_id which) noexcept
        -> std::size_t
    {
        constexpr auto xsize = (sector_size - mArchiveHeaderOffset) / 2;
        return xsize;
    }

    inline auto sector_device::header_offset(header_id which) const noexcept
        -> std::size_t
    {
        return mArchiveHeaderOffset + ((-static_cast<std::size_t>(which)) &
                                       header_size(header_id::first));
    }
    inline void sector_device::switch_header() noexcept
    {
        mHeaderSelector = header_id{
            !static_cast<std::underlying_type_t<header_id>>(mHeaderSelector)};
    }

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(pop)
#endif
} // namespace vefs::detail
