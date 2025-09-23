#pragma once

#include <algorithm>
#include <mutex>

#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>

#include <vefs/disappointment.hpp>

#include "archive_sector_allocator.hpp"
#include "sector_id.hpp"

namespace vefs::detail
{
/**
 * Copy-on-write allocator for sector trees.
 *
 * Uses an underlying allocator (the SourceAllocator) to re-allocate sectors for
 * a subsequent write operation. The old sectors produced by call to
 * reallocate() are kept until commit() is called and are reused in later
 * calls to reallocate().
 */
template <typename SourceAllocator>
class cow_tree_allocator_mt final
{
    static constexpr auto max_buffered_allocation = 128;
    using id_buffer_type
            = boost::container::static_vector<sector_id,
                                              max_buffered_allocation>;
    using overwritten_id_container_type
            = boost::container::small_vector<sector_id,
                                             max_buffered_allocation>;

public:
    using source_allocator_type = SourceAllocator;

    enum class leak_on_failure_t
    {
    };
    static constexpr auto leak_on_failure = leak_on_failure_t{};

    class sector_allocator
    {
        friend class cow_tree_allocator_mt<SourceAllocator>;

    public:
        sector_allocator(cow_tree_allocator_mt &, sector_id current)
            : current_allocation(current)
            , allocation_commit(-1)
        {
        }

    private:
        sector_id current_allocation;
        long long allocation_commit;
    };

    cow_tree_allocator_mt(archive_sector_allocator &sourceAllocator)
        : mSourceAllocator(sourceAllocator)
        , mCommitCounter(0)
        , mBufferSync()
        , mAllocationBuffer()
        , mDeallocationSync()
        , mOverwrittenAllocations()
    {
    }
    ~cow_tree_allocator_mt()
    {
        if (!mOverwrittenAllocations.empty())
        {
            on_leak_detected();
        }
        for (auto allocation : mAllocationBuffer)
        {
            mSourceAllocator.dealloc_one(
                    allocation, source_allocator_type::leak_on_failure);
        }
    }

    auto reallocate(sector_allocator &forWhich) noexcept -> result<sector_id>
    {
        if (mCommitCounter == forWhich.allocation_commit)
        {
            return forWhich.current_allocation;
        }
        sector_id allocation = try_alloc_from_buffer_mt();
        if (allocation == sector_id{})
        {
            if (auto sourceAllocRx = mSourceAllocator.alloc_one())
            {
                allocation = sourceAllocRx.assume_value();
            }
            else
            {
                return std::move(sourceAllocRx).as_failure();
            }
        }
        forWhich.allocation_commit = mCommitCounter;

        if (auto prevAllocation
            = std::exchange(forWhich.current_allocation, allocation);
            prevAllocation != sector_id{})
        {
            try
            {
                std::lock_guard deallocationLock{mDeallocationSync};
                mOverwrittenAllocations.push_back(prevAllocation);
            }
            catch (std::bad_alloc const &)
            {
                on_leak_detected();
            }
        }
        return allocation;
    }

    auto dealloc(sector_allocator &part) noexcept -> result<void>
    {
        if (part.allocation_commit >= 0)
        {
            VEFS_TRY(dealloc_one(part.current_allocation));
            part = {*this, sector_id{}};
        }
        return oc::success();
    }
    void dealloc(sector_allocator &part, leak_on_failure_t) noexcept
    {
        if (part.allocation_commit >= 0)
        {
            dealloc_one(part.current_allocation, leak_on_failure);
        }
        part = {*this, sector_id{}};
    }
    auto dealloc_one(sector_id const which) noexcept -> result<void>
    {
        try
        {
            std::lock_guard deallocationLock{mDeallocationSync};
            mOverwrittenAllocations.push_back(which);
            return success();
        }
        catch (std::bad_alloc const &)
        {
            return errc::not_enough_memory;
        }
    }
    void dealloc_one(sector_id const which, leak_on_failure_t) noexcept
    {
        if (!dealloc_one(which))
        {
            on_leak_detected();
        }
    }

    auto on_commit() noexcept -> result<void>
    {
        mCommitCounter += 1;
        std::scoped_lock const lock{mBufferSync, mDeallocationSync};

        auto const bufferAmount = std::min(mAllocationBuffer.capacity()
                                                   - mAllocationBuffer.size(),
                                           mOverwrittenAllocations.size());
        auto const split
                = std::next(mOverwrittenAllocations.begin(), bufferAmount);

        std::copy(mOverwrittenAllocations.begin(), split,
                  std::back_inserter(mAllocationBuffer));

        std::for_each(split, mOverwrittenAllocations.end(),
                      [this](sector_id allocation) {
                          mSourceAllocator.dealloc_one(
                                  allocation,
                                  source_allocator_type::leak_on_failure);
                      });

        mOverwrittenAllocations.clear();
        mOverwrittenAllocations.shrink_to_fit();

        return success();
    }

    void on_leak_detected() noexcept
    {
        mSourceAllocator.on_leak_detected();
    }

private:
    auto try_alloc_from_buffer_mt() -> sector_id
    {
        std::lock_guard bufferLock{mBufferSync};
        if (mAllocationBuffer.empty())
        {
            return sector_id{};
        }
        else
        {
            auto allocation = mAllocationBuffer.back();
            mAllocationBuffer.pop_back();
            return allocation;
        }
    }

    source_allocator_type &mSourceAllocator;
    long long mCommitCounter;
    std::mutex mBufferSync;
    id_buffer_type mAllocationBuffer;
    std::mutex mDeallocationSync;
    overwritten_id_container_type mOverwrittenAllocations;
};

extern template class cow_tree_allocator_mt<archive_sector_allocator>;
} // namespace vefs::detail
