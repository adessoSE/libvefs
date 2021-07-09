#pragma once

#include <atomic>
#include <memory>

#include <vefs/platform/thread_pool.hpp>
#include <vefs/utils/dirt_flag.hpp>

#include "detail/archive_file_id.hpp"
#include "detail/archive_sector_allocator.hpp"
#include "detail/cow_tree_allocator_mt.hpp"
#include "detail/root_sector_info.hpp"
#include "detail/sector_tree_mt.hpp"

namespace vefs
{
class vfilesystem;

class vfile
{
    using tree_type = detail::sector_tree_mt<
            detail::cow_tree_allocator_mt<detail::archive_sector_allocator>,
            detail::thread_pool>;

    struct inacessible_ctor
    {
    };

public:
    vfile(vfilesystem *owner,
          detail::thread_pool &executor,
          detail::file_id id,
          std::uint64_t maximumExtent,
          inacessible_ctor);
    ~vfile();

    static auto open_existing(vfilesystem *owner,
                              detail::thread_pool &executor,
                              detail::archive_sector_allocator &allocator,
                              detail::file_id id,
                              detail::sector_device &device,
                              detail::file_crypto_ctx &cryptoCtx,
                              detail::root_sector_info treeRoot)
            -> result<std::shared_ptr<vfile>>;
    static auto create_new(vfilesystem *owner,
                           detail::thread_pool &executor,
                           detail::archive_sector_allocator &allocator,
                           detail::file_id id,
                           detail::sector_device &device,
                           detail::file_crypto_ctx &cryptoCtx)
            -> result<std::shared_ptr<vfile>>;

    auto read(rw_dynblob buffer, std::uint64_t readPos) -> result<void>;
    auto write(ro_dynblob data, std::uint64_t writePos) -> result<void>;

    auto maximum_extent() -> std::uint64_t;
    auto truncate(std::uint64_t size) -> result<void>;

    auto commit() -> result<void>;
    auto is_dirty() -> bool
    {
        return mWriteFlag.is_dirty();
    }

private:
    auto open_existing(detail::sector_device &device,
                       detail::file_crypto_ctx &cryptoCtx,
                       detail::archive_sector_allocator &allocator,
                       detail::root_sector_info treeRoot) -> result<void>;
    auto create_new(detail::sector_device &device,
                    detail::archive_sector_allocator &allocator,
                    detail::file_crypto_ctx &cryptoCtx) -> result<void>;

    auto sync_commit_info(detail::root_sector_info committedRootInfo) noexcept
            -> result<void>;

    vfilesystem *mOwner;
    detail::file_id mId;

    std::unique_ptr<tree_type> mFileTree;
    std::atomic<std::uint64_t> mMaximumExtent;
    utils::dirt_flag mWriteFlag;

    std::mutex mCommitSync;
    detail::pooled_work_tracker mWorkTracker;
};

} // namespace vefs
