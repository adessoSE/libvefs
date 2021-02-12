#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include <vefs/archive_fwd.hpp>
#include <vefs/disappointment.hpp>
#include <vefs/platform/thread_pool.hpp>
#include <vefs/span.hpp>
#include <vefs/utils/enum_bitset.hpp>
#include <vefs/utils/ref_ptr.hpp>

#include <vefs/llfio.hpp>

namespace vefs
{
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

    class vfilesystem;
    class vfile;
    using vfile_handle = std::shared_ptr<vfile>;

    class archive
    {
    public:
        static auto open(llfio::mapped_file_handle mfh,
                         crypto::crypto_provider *cryptoProvider,
                         ro_blob<32> userPRK, bool createNew)
            -> result<std::unique_ptr<archive>>;
        static auto validate(llfio::mapped_file_handle mfh,
                             crypto::crypto_provider *cryptoProvider,
                             ro_blob<32> userPRK) -> result<void>;
        ~archive();

        auto commit() -> result<void>;

        auto open(const std::string_view filePath,
                  const file_open_mode_bitset mode) -> result<vfile_handle>;
        auto query(const std::string_view filePath)
            -> result<file_query_result>;
        auto erase(std::string_view filePath) -> result<void>;
        auto read(const vfile_handle &handle, rw_dynblob buffer,
                  std::uint64_t readFilePos) -> result<void>;
        auto write(const vfile_handle &handle, ro_dynblob data,
                   std::uint64_t writeFilePos) -> result<void>;
        auto truncate(const vfile_handle &handle, std::uint64_t maxExtent)
            -> result<void>;
        auto maximum_extent_of(const vfile_handle &handle)
            -> result<std::uint64_t>;
        auto commit(const vfile_handle &handle) -> result<void>;

        auto personalization_area() noexcept -> std::span<std::byte, 1 << 12>;
        auto sync_personalization_area() noexcept -> result<void>;

    private:
        archive(std::unique_ptr<detail::sector_device> primitives);
        detail::thread_pool &ops_pool();

        std::unique_ptr<detail::sector_device> mArchive;
        std::unique_ptr<detail::archive_sector_allocator> mSectorAllocator;
        std::unique_ptr<vfilesystem> mFilesystem;

        detail::pooled_work_tracker mWorkTracker;
    };

    auto read_archive_personalization_area(
        llfio::path_handle const &base, llfio::path_view where,
        std::span<std::byte, 1 << 12> out) noexcept -> result<void>;

    inline detail::thread_pool &vefs::archive::ops_pool()
    {
        return mWorkTracker;
    }

} // namespace vefs
