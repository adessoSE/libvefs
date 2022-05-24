#pragma once

#include <cstdint>

#include <limits>
#include <memory>
#include <type_traits>

#include <vefs/archive.hpp>
#include <vefs/llfio.hpp>
#include <vefs/platform/thread_pool.hpp>
#include <vefs/utils/dirt_flag.hpp>
#include <vefs/utils/unordered_map_mt.hpp>

#include "detail/archive_file_id.hpp"
#include "detail/archive_sector_allocator.hpp"
#include "detail/block_manager.hpp"
#include "detail/cow_tree_allocator_mt.hpp"
#include "detail/file_crypto_ctx.hpp"
#include "detail/root_sector_info.hpp"
#include "detail/sector_device.hpp"
#include "detail/sector_tree_mt.hpp"
#include "vfile.hpp"

namespace vefs
{

struct vfilesystem_entry final
{
    int32_t index_file_position;
    int32_t num_reserved_blocks;

    std::unique_ptr<detail::file_crypto_ctx> crypto_ctx;
    std::weak_ptr<vfile> instance;

    bool needs_index_update;

    detail::root_sector_info tree_info;
};

class vfilesystem final
{
    using index_t = utils::unordered_string_map_mt<detail::file_id>;
    using files_t = utils::unordered_map_mt<detail::file_id, vfilesystem_entry>;

    using tree_type = detail::sector_tree_mt<
            detail::cow_tree_allocator_mt<detail::archive_sector_allocator>,
            detail::thread_pool>;
    using block_manager = utils::block_manager<int>;

    class index_tree_layout;

public:
    vfilesystem(detail::sector_device &device,
                detail::archive_sector_allocator &allocator,
                detail::thread_pool &executor,
                detail::master_file_info const &info);

    static auto open_existing(detail::sector_device &device,
                              detail::archive_sector_allocator &allocator,
                              detail::thread_pool &executor,
                              detail::master_file_info const &info)
            -> result<std::unique_ptr<vfilesystem>>;
    static auto create_new(detail::sector_device &device,
                           detail::archive_sector_allocator &allocator,
                           detail::thread_pool &executor,
                           detail::master_file_info const &info)
            -> result<std::unique_ptr<vfilesystem>>;

    auto open(const std::string_view filePath, const file_open_mode_bitset mode)
            -> result<vfile_handle>;
    auto open(const detail::file_id id) -> result<vfile_handle>;
    auto erase(std::string_view filePath) -> result<void>;
    /**
     * Extracts a vfile at the given path as a physical file on the
     * device in the given path.
     *
     * @param sourceFilePath the path to the vfile that should be extracted
     * @param targetBasePath the path to the output file
     */
    auto extract(llfio::path_view sourceFilePath,
                 llfio::path_view targetBasePath) -> result<void>;
    /**
     * Extracts all available vfiles as physical files on the device
     * in their according paths.
     *
     * @param targetBasePath the path to which the content should be extracted
     * to
     */
    auto extractAll(llfio::path_view targetBasePath) -> result<void>;

    auto query(const std::string_view filePath) -> result<file_query_result>;

    auto on_vfile_commit(detail::file_id fileId,
                         detail::root_sector_info updatedRootInfo)
            -> result<void>;

    auto commit() -> result<void>;
    auto crypto_ctx() const noexcept -> detail::file_crypto_ctx const &
    {
        return mCryptoCtx;
    }
    auto committed_root() const noexcept -> detail::root_sector_info
    {
        return mCommittedRoot;
    }

    auto recover_unused_sectors() -> result<void>;
    auto validate() -> result<void>;
    auto replace_corrupted_sectors() -> result<void>;

private:
    auto open_existing_impl() -> result<void>;
    auto create_new_impl() -> result<void>;

    auto sync_commit_info(detail::root_sector_info rootInfo,
                          std::uint64_t maxExtent) noexcept -> result<void>;
    template <typename OpenFn>
    auto extract(llfio::path_view sourceFilePath,
                 llfio::path_view targetBasePath,
                 OpenFn &&open) -> result<void>;

    detail::sector_device &mDevice;
    detail::archive_sector_allocator &mSectorAllocator;
    detail::thread_pool &mDeviceExecutor;

    detail::file_crypto_ctx mCryptoCtx;
    detail::root_sector_info mCommittedRoot;

    index_t mIndex;
    files_t mFiles;
    block_manager mIndexBlocks;
    std::unique_ptr<tree_type> mIndexTree;
    utils::dirt_flag mWriteFlag;
    std::mutex mIOSync;
};

} // namespace vefs
