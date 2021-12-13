#pragma once

#include <array>
#include <memory>
#include <shared_mutex>
#include <type_traits>

#include <dplx/dp/memory_buffer.hpp>

#include <vefs/llfio.hpp>

#include <vefs/disappointment.hpp>
#include <vefs/disappointment/llfio_adapter.hpp>
#include <vefs/span.hpp>
#include <vefs/utils/secure_array.hpp>

#include "../crypto/counter.hpp"
#include "../crypto/provider.hpp"
#include "archive_header.hpp"
#include "file_crypto_ctx.hpp"
#include "file_descriptor.hpp"
#include "io_buffer_manager.hpp"
#include "root_sector_info.hpp"
#include "sector_id.hpp"

namespace vefs::detail
{
struct master_file_info
{
    file_crypto_ctx::state_type crypto_state;
    root_sector_info tree_info;

    master_file_info() noexcept = default;
    explicit master_file_info(file_descriptor const &desc)
        : crypto_state{.secret
                       = utils::secure_byte_array<32>(span(desc.secret)),
                       .counter = desc.secretCounter}
        , tree_info(desc.data)
    {
    }
};

struct master_header
{
    utils::secure_byte_array<64> master_secret;
    crypto::atomic_counter master_counter;
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

    struct open_info
    {
        std::unique_ptr<sector_device> device;
        master_file_info filesystem_index;
        master_file_info free_sector_index;
    };

    static constexpr size_t sector_size = 1 << 15; // 2^15
    static constexpr size_t sector_payload_size
            = sector_size - (1 << 5); // 2^15-2^5

    static constexpr std::size_t static_header_size = 1 << 12;
    static constexpr std::size_t personalization_area_size = 1 << 12;
    static constexpr std::size_t pheader_size = (1 << 13) + (1 << 12);

    static constexpr auto to_offset(sector_id id) -> std::uint64_t;

    static auto open_existing(llfio::file_handle fileHandle,
                              crypto::crypto_provider *cryptoProvider,
                              ro_blob<32> userPRK) noexcept
            -> result<open_info>;
    static auto create_new(llfio::file_handle fileHandle,
                           crypto::crypto_provider *cryptoProvider,
                           ro_blob<32> userPRK) noexcept -> result<open_info>;

    ~sector_device() = default;

    auto read_sector(rw_blob<sector_payload_size> contentDest,
                     const file_crypto_ctx &fileCtx,
                     sector_id sectorIdx,
                     ro_blob<16> contentMAC) noexcept -> result<void>;

    template <typename file_crypto_ctx_T = file_crypto_ctx>
    auto write_sector(rw_blob<16> mac,
                      file_crypto_ctx_T &fileCtx,
                      sector_id sectorIdx,
                      ro_blob<sector_payload_size> data) noexcept
            -> result<void>;
    auto erase_sector(sector_id sectorIdx) noexcept -> result<void>;

    auto personalization_area() noexcept
            -> std::span<std::byte, personalization_area_size>;
    auto sync_personalization_area() noexcept -> result<void>;

    auto update_header(file_crypto_ctx const &filesystemIndexCtx,
                       root_sector_info filesystemIndexRoot,
                       file_crypto_ctx const &freeSectorIndex,
                       root_sector_info freeSectorIndexRoot) -> result<void>;
    result<void> update_static_header(ro_blob<32> newUserPRK);

    // numSectors = number of sectors (i.e. including the master sector)
    result<void> resize(std::uint64_t numSectors);
    std::uint64_t size() const;

    ro_blob<64> master_secret_view() const;
    ro_blob<16> session_salt_view() const;

    const crypto::crypto_provider *crypto() const;
    crypto::atomic_counter &master_secret_counter();

    auto create_file_secrets() noexcept
            -> result<std::unique_ptr<file_crypto_ctx>>;

    auto create_file_secrets2() noexcept -> result<file_crypto_ctx::state_type>;

private:
    sector_device(llfio::file_handle mfh,
                  crypto::crypto_provider *cryptoProvider,
                  size_t numSectors);

    auto parse_static_archive_header(ro_blob<32> userPRK) -> result<void>;
    auto parse_archive_header() -> result<archive_header>;
    auto parse_archive_header(header_id which) -> result<archive_header>;

    result<void> write_static_archive_header(ro_blob<32> userPRK);

    static constexpr auto header_size(header_id which) noexcept -> std::size_t;
    constexpr auto header_offset(header_id which) const noexcept -> std::size_t;
    void switch_header() noexcept;

    crypto::crypto_provider *const mCryptoProvider;
    llfio::file_handle mArchiveFile;
    llfio::unique_file_lock mArchiveFileLock;

    dplx::dp::memory_allocation<llfio::utils::page_allocator<std::byte>>
            mMasterSector;
    io_buffer_manager mIoBufferManager;

    master_header mStaticHeader;
    utils::secure_byte_array<16> mSessionSalt;
    crypto::atomic_counter mArchiveSecretCounter;
    crypto::atomic_counter mJournalCounter;
    std::atomic<std::uint64_t> mEraseCounter;

    std::atomic<uint64_t> mNumSectors;

    header_id mHeaderSelector;
};
static_assert(!std::is_default_constructible_v<sector_device>);
static_assert(!std::is_copy_constructible_v<sector_device>);
static_assert(!std::is_copy_assignable_v<sector_device>);
static_assert(!std::is_move_constructible_v<sector_device>);
static_assert(!std::is_move_assignable_v<sector_device>);

auto read_archive_personalization_area(
        llfio::file_handle &file, std::span<std::byte, 1 << 12> out) noexcept
        -> result<void>;

constexpr std::uint64_t sector_device::to_offset(sector_id id)
{
    return static_cast<std::uint64_t>(id) * sector_size;
}

inline auto vefs::detail::sector_device::resize(std::uint64_t numSectors)
        -> result<void>
{
    VEFS_TRY(auto &&bytesTruncated,
             mArchiveFile.truncate(numSectors * sector_size));
    if (bytesTruncated != (numSectors * sector_size))
    {
        return errc::bad;
    }
    mNumSectors.store(numSectors, std::memory_order::relaxed);

    return success();
}
inline std::uint64_t sector_device::size() const
{
    return mNumSectors.load(std::memory_order::relaxed);
}

inline ro_blob<64> sector_device::master_secret_view() const
{
    return as_span(mStaticHeader.master_secret);
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

constexpr auto sector_device::header_offset(header_id which) const noexcept
        -> std::size_t
{
    return static_header_size + personalization_area_size
         + ((-static_cast<std::size_t>(which)) & pheader_size);
}
inline void sector_device::switch_header() noexcept
{
    mHeaderSelector = header_id{
            !static_cast<std::underlying_type_t<header_id>>(mHeaderSelector)};
}

inline auto sector_device::personalization_area() noexcept
        -> std::span<std::byte, personalization_area_size>
{
    return mMasterSector.as_span()
            .subspan<static_header_size, personalization_area_size>();
}

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(pop)
#endif
} // namespace vefs::detail
