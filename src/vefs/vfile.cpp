#include "vfile.hpp"

#include "vfilesystem.hpp"

template class vefs::detail::sector_tree_mt<vefs::detail::cow_tree_allocator_mt<
        vefs::detail::archive_sector_allocator>>;

namespace vefs
{

vfile::vfile(vfilesystem *owner,
             detail::thread_pool &executor,
             detail::file_id id,
             std::uint64_t maximumExtent,
             inacessible_ctor)
    : mOwner(owner)
    , mId(id)
    , mFileTree()
    , mMaximumExtent(maximumExtent)
    , mWriteFlag()
    , mCommitSync()
    , mWorkTracker(&executor)
{
}

vfile::~vfile()
{
    if (mFileTree)
    {
        mWorkTracker.wait();
        mFileTree.reset();
    }
}

auto vfile::open_existing(vfilesystem *owner,
                          detail::thread_pool &executor,
                          detail::archive_sector_allocator &allocator,
                          detail::file_id id,
                          detail::sector_device &device,
                          detail::file_crypto_ctx &cryptoCtx,
                          detail::root_sector_info treeRoot)
        -> result<std::shared_ptr<vfile>>
try
{
    auto self = std::make_shared<vfile>(
            owner, executor, id, treeRoot.maximum_extent, inacessible_ctor{});

    VEFS_TRY(self->open_existing(device, cryptoCtx, allocator, treeRoot));

    return self;
}
catch (std::bad_alloc const &)
{
    return errc::not_enough_memory;
}

auto vfile::open_existing(detail::sector_device &device,
                          detail::file_crypto_ctx &cryptoCtx,
                          detail::archive_sector_allocator &allocator,
                          detail::root_sector_info treeRoot) -> result<void>
{
    VEFS_TRY(mFileTree,
             tree_type::open_existing(device, cryptoCtx, treeRoot, allocator));

    return success();
}

auto vfile::create_new(vfilesystem *owner,
                       detail::thread_pool &executor,
                       detail::archive_sector_allocator &allocator,
                       detail::file_id id,
                       detail::sector_device &device,
                       detail::file_crypto_ctx &cryptoCtx)
        -> result<std::shared_ptr<vfile>>
try
{
    auto self = std::make_shared<vfile>(owner, executor, id, 0,
                                        inacessible_ctor{});

    VEFS_TRY(self->create_new(device, allocator, cryptoCtx));

    return self;
}
catch (std::bad_alloc const &)
{
    return errc::not_enough_memory;
}

auto vfile::create_new(detail::sector_device &device,
                       detail::archive_sector_allocator &allocator,
                       detail::file_crypto_ctx &cryptoCtx) -> result<void>
{
    VEFS_TRY(mFileTree, tree_type::create_new(device, cryptoCtx, allocator));

    mWriteFlag.mark();
    return success();
}

auto vfile::read(rw_dynblob buffer, std::uint64_t readPos) -> result<void>
{
    return detail::read(*mFileTree, buffer, readPos);
}

auto vfile::write(ro_dynblob data, std::uint64_t writePos) -> result<void>
{
    if (auto maxExtent = mMaximumExtent.load(std::memory_order_acquire);
        maxExtent < writePos)
    {
        VEFS_TRY(truncate(writePos));
    }

    VEFS_TRY(detail::write(*mFileTree, data, writePos));

    auto writeExtent = writePos + data.size();
    auto maxExtent = mMaximumExtent.load(std::memory_order_acquire);
    while (maxExtent < writeExtent
           && !mMaximumExtent.compare_exchange_weak(maxExtent, writeExtent,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire))
    {
    }
    mWriteFlag.mark();
    return success();
}

auto vefs::vfile::extract(llfio::file_handle &fileHandle) -> result<void>
{
    return detail::extract(*mFileTree, fileHandle, 0,
                           mMaximumExtent.load(std::memory_order_acquire));
}

auto vfile::maximum_extent() -> std::uint64_t
{
    return mMaximumExtent.load(std::memory_order_acquire);
}

auto vfile::truncate(std::uint64_t size) -> result<void>
{
    using detail::lut::sector_position_of;

    auto maximumExtent = mMaximumExtent.load(std::memory_order_acquire);

retry:
    auto it = maximumExtent ? sector_position_of(maximumExtent - 1) : 0;
    auto end = size ? sector_position_of(size - 1) : 0;

    if (it < end)
    {
        do
        {
            VEFS_TRY(mFileTree->access_or_create(detail::tree_position(it)));
            mWriteFlag.mark();

            auto newSize = std::min(
                    it * detail::sector_device::sector_payload_size, size);

            while (!mMaximumExtent.compare_exchange_weak(
                    maximumExtent, newSize, std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                newSize = std::max(newSize, maximumExtent);
            }

        } while (++it <= end);
    }
    else if (it > end)
    {
        do
        {
            VEFS_TRY(mFileTree->erase_leaf(it));
            mWriteFlag.mark();

            auto newSize = std::max(
                    it * detail::sector_device::sector_payload_size, size);

            while (!mMaximumExtent.compare_exchange_weak(
                    maximumExtent, newSize, std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                newSize = std::min(newSize, maximumExtent);
            }

        } while (--it != 0 && it >= end);

        if (end == 0)
        {
            auto newSize = size;
            while (!mMaximumExtent.compare_exchange_weak(
                    maximumExtent, newSize, std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                newSize = std::min(newSize, maximumExtent);
            }
        }
    }
    else
    {
        if (!mMaximumExtent.compare_exchange_weak(maximumExtent, size,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire))
        {
            goto retry;
        }
    }

    return success();
}

auto vfile::commit() -> result<void>
{
    if (!mWriteFlag.is_dirty())
    {
        return success();
    }

    mWriteFlag.unmark();

    auto commitRx = mFileTree->commit(
            [this](detail::root_sector_info committedRootInfo) noexcept
            { return sync_commit_info(committedRootInfo); });
    if (!commitRx)
    {
        mWriteFlag.mark();
        return std::move(commitRx).as_failure();
    }

    return success();
}

auto vfile::sync_commit_info(
        detail::root_sector_info committedRootInfo) noexcept -> result<void>
{
    committedRootInfo.maximum_extent
            = mMaximumExtent.load(std::memory_order_acquire);

    return mOwner->on_vfile_commit(mId, committedRootInfo);
}

} // namespace vefs
