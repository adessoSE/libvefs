#include "preallocated_tree_allocator.hpp"

namespace vefs::detail
{
    preallocated_tree_allocator::sector_allocator::sector_allocator(
        preallocated_tree_allocator &, sector_id current) noexcept
        : mCurrent(current)
    {
    }

    preallocated_tree_allocator::preallocated_tree_allocator(
        sector_id_container &ids) noexcept
        : mIds(ids)
        , mLeaked(false)
    {
    }

    auto
    preallocated_tree_allocator::reallocate(sector_allocator &part) noexcept
        -> result<sector_id>
    {
        if (part.mCurrent != sector_id{})
        {
            return part.mCurrent;
        }
        if (mIds.empty())
        {
            return errc::resource_exhausted;
        }
        auto allocated = mIds.back();
        mIds.pop_back();
        return allocated;
    }

    auto
    preallocated_tree_allocator::dealloc_one(const sector_id which) noexcept
        -> result<void>
    {
        try
        {
            mIds.push_back(which);
            return success();
        }
        catch (const std::bad_alloc &)
        {
            return errc::not_enough_memory;
        }
    }

    void preallocated_tree_allocator::dealloc_one(const sector_id which,
                                                  leak_on_failure_t) noexcept
    {
        if (!dealloc_one(which))
        {
            on_leak_detected();
        }
    }

    auto preallocated_tree_allocator::on_commit() noexcept -> result<void>
    {
        return success();
    }

    void preallocated_tree_allocator::on_leak_detected() noexcept
    {
        mLeaked = true;
    }
    auto preallocated_tree_allocator::leaked() -> bool
    {
        return mLeaked;
    }
    void preallocated_tree_allocator::reset_leak_flag()
    {
        mLeaked = false;
    }
} // namespace vefs::detail
