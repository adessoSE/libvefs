#pragma once

#include "vefs/disappointment.hpp"

#include "archive_sector_allocator.hpp"
#include "sector_id.hpp"

namespace vefs::detail
{

/**
 * Allocator for a single sector tree. Uses the \ref archive_sector_allocator
 * internally to allocate/deallocate sectors from the archive.
 */
class archive_tree_allocator
{
public:
    class sector_allocator
    {
        friend class archive_tree_allocator;

    public:
        sector_allocator(archive_tree_allocator &, sector_id current)
            : mCurrent(current)
        {
        }

    private:
        sector_id mCurrent;
    };

    enum class leak_on_failure_t
    {
    };
    static constexpr auto leak_on_failure = leak_on_failure_t{};

    explicit archive_tree_allocator(archive_sector_allocator &source)
        : mSource(source)
    {
    }

    auto reallocate(sector_allocator &part) noexcept -> result<sector_id>
    {
        if (part.mCurrent != sector_id{})
        {
            return part.mCurrent;
        }
        VEFS_TRY(part.mCurrent, mSource.alloc_one());
        return part.mCurrent;
    }

    auto dealloc(sector_allocator &part) noexcept -> result<void>
    {
        if (part.mCurrent != sector_id{})
        {
            VEFS_TRY(mSource.dealloc_one(part.mCurrent));
            part.mCurrent = sector_id{};
        }
        return oc::success();
    }
    void dealloc(sector_allocator &part, leak_on_failure_t) noexcept
    {
        if (part.mCurrent != sector_id{})
        {
            mSource.dealloc_one(std::exchange(part.mCurrent, sector_id{}),
                                archive_sector_allocator::leak_on_failure);
        }
    }
    auto dealloc_one(sector_id const which) noexcept -> result<void>
    {
        return mSource.dealloc_one(which);
    }
    void dealloc_one(sector_id const which, leak_on_failure_t) noexcept
    {
        mSource.dealloc_one(which, archive_sector_allocator::leak_on_failure);
    }

    auto on_commit() noexcept -> result<void>
    {
        return success();
    }

    void on_leak_detected() noexcept
    {
        mSource.on_leak_detected();
    }

private:
    archive_sector_allocator &mSource;
};

} // namespace vefs::detail
