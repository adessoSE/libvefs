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
            : current(current)
        {
        }

    private:
        sector_id current;
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
        if (part.current != sector_id{})
        {
            return part.current;
        }
        VEFS_TRY(part.current, mSource.alloc_one());
        return part.current;
    }

    auto dealloc_one(const sector_id which) noexcept -> result<void>
    {
        return mSource.dealloc_one(which);
    }

    void dealloc_one(const sector_id which, leak_on_failure_t) noexcept
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
