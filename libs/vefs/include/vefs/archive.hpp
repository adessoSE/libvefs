#pragma once

#include <atomic>
#include <memory>
#include <span>
#include <string_view>
#include <tuple>

#include <vefs/archive_fwd.hpp>
#include <vefs/disappointment.hpp>
#include <vefs/llfio.hpp>
#include <vefs/span.hpp>
#include <vefs/utils/enum_bitset.hpp>

namespace vefs
{

enum class creation
{
    open_existing = 0,
    only_if_not_exist = 1,
    if_needed = 2,
    always_new = 4,
};

enum class file_open_mode
{
    read = 0b0000,
    write = 0b0001,
    readwrite = read | write,
    truncate = 0b0010,
    create = 0b0100,
};

std::true_type allow_enum_bitset(file_open_mode &&);
using file_open_mode_bitset = enum_bitset<file_open_mode>;

struct file_query_result
{
    file_open_mode_bitset allowed_flags;
    std::size_t size;
};

class archive_handle
{
    using sector_device_owner = std::unique_ptr<detail::sector_device>;
    using sector_allocator_owner
            = std::unique_ptr<detail::archive_sector_allocator>;
    using vfilesystem_owner = std::unique_ptr<vfilesystem>;
    using work_tracker_owner = std::unique_ptr<detail::pooled_work_tracker>;

    sector_device_owner mArchive;
    sector_allocator_owner mSectorAllocator;
    work_tracker_owner mWorkTracker;
    vfilesystem_owner mFilesystem;

public:
    ~archive_handle();
    archive_handle() noexcept;

    archive_handle(archive_handle const &) noexcept = delete;
    auto operator=(archive_handle const &) noexcept
            -> archive_handle & = delete;

    archive_handle(archive_handle &&other) noexcept;
    auto operator=(archive_handle &&other) noexcept -> archive_handle &;

    friend inline void swap(archive_handle &lhs, archive_handle &rhs) noexcept
    {
        using std::swap;
        swap(lhs.mArchive, rhs.mArchive);
        swap(lhs.mSectorAllocator, rhs.mSectorAllocator);
        swap(lhs.mWorkTracker, rhs.mWorkTracker);
        swap(lhs.mFilesystem, rhs.mFilesystem);
    }

    using creation = vefs::creation;

    archive_handle(sector_device_owner sectorDevice,
                   sector_allocator_owner sectorAllocator,
                   work_tracker_owner workTracker,
                   vfilesystem_owner filesystem) noexcept;

    static auto archive(llfio::file_handle const &file,
                        ro_blob<32> userPRK,
                        crypto::crypto_provider *cryptoProvider
                        = crypto::boringssl_aes_256_gcm_crypto_provider(),
                        creation creationMode = creation::open_existing)
            -> result<archive_handle>;
    static auto archive(llfio::path_handle const &base,
                        llfio::path_view path,
                        ro_blob<32> userPRK,
                        crypto::crypto_provider *cryptoProvider
                        = crypto::boringssl_aes_256_gcm_crypto_provider(),
                        creation creationMode = creation::open_existing)
            -> result<archive_handle>;

    static auto purge_corruption(llfio::path_handle const &base,
                                 llfio::path_view path,
                                 ro_blob<32> userPRK,
                                 crypto::crypto_provider *cryptoProvider)
            -> result<void>;
    static auto validate(llfio::path_handle const &base,
                         llfio::path_view path,
                         ro_blob<32> userPRK,
                         crypto::crypto_provider *cryptoProvider)
            -> result<void>;

    auto commit() -> result<void>;

    auto open(const std::string_view filePath, const file_open_mode_bitset mode)
            -> result<vfile_handle>;
    auto query(const std::string_view filePath) -> result<file_query_result>;
    auto erase(std::string_view filePath) -> result<void>;
    auto read(const vfile_handle &handle,
              rw_dynblob buffer,
              std::uint64_t readFilePos) -> result<void>;
    auto write(const vfile_handle &handle,
               ro_dynblob data,
               std::uint64_t writeFilePos) -> result<void>;
    auto truncate(const vfile_handle &handle, std::uint64_t maxExtent)
            -> result<void>;
    auto maximum_extent_of(const vfile_handle &handle) -> result<std::uint64_t>;
    auto commit(const vfile_handle &handle) -> result<void>;

    auto personalization_area() noexcept -> std::span<std::byte, 1 << 12>;
    auto sync_personalization_area() noexcept -> result<void>;

private:
    auto ops_pool() -> detail::pooled_work_tracker &;

    static auto open_existing(llfio::mapped_file_handle mfh,
                              crypto::crypto_provider *cryptoProvider,
                              ro_blob<32> userPRK) noexcept
            -> result<archive_handle>;
    static auto create_new(llfio::mapped_file_handle mfh,
                           crypto::crypto_provider *cryptoProvider,
                           ro_blob<32> userPRK) noexcept
            -> result<archive_handle>;

    static auto purge_corruption(llfio::mapped_file_handle &&file,
                                 ro_blob<32> userPRK,
                                 crypto::crypto_provider *cryptoProvider)
            -> result<void>;
};

inline auto vefs::archive_handle::ops_pool() -> detail::pooled_work_tracker &
{
    return *mWorkTracker;
}

inline auto archive(llfio::file_handle const &file,
                    ro_blob<32> userPRK,
                    crypto::crypto_provider *cryptoProvider
                    = crypto::boringssl_aes_256_gcm_crypto_provider(),
                    creation creationMode = creation::open_existing)
{
    return archive_handle::archive(file, userPRK, cryptoProvider, creationMode);
}
inline auto archive(llfio::path_handle const &base,
                    llfio::path_view path,
                    ro_blob<32> userPRK,
                    crypto::crypto_provider *cryptoProvider
                    = crypto::boringssl_aes_256_gcm_crypto_provider(),
                    creation creationMode = creation::open_existing)
        -> result<archive_handle>
{
    return archive_handle::archive(base, std::move(path), userPRK,
                                   cryptoProvider, creationMode);
}

auto read_archive_personalization_area(
        llfio::path_handle const &base,
        llfio::path_view where,
        std::span<std::byte, 1 << 12> out) noexcept -> result<void>;

} // namespace vefs
