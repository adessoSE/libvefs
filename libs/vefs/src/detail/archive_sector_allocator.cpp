#include "archive_sector_allocator.hpp"

#include <cassert>

namespace vefs::detail
{
    auto vefs::detail::archive_sector_allocator::alloc_one() noexcept
        -> result<sector_id>
    {
        std::lock_guard allocGuard{mAllocatorSync};

        if (auto allocationrx = mSectorManager.alloc_one();
            allocationrx ||
            allocationrx.assume_error() != errc::resource_exhausted)
        {
            return allocationrx;
        }

        VEFS_TRY(mine_new(4));

        return mSectorManager.alloc_one();
    }

    void archive_sector_allocator::dealloc_one(sector_id which) noexcept
    {
        if (!mSectorManager.dealloc_one(which))
        {
            sectors_leaked();
        }
    }

    auto archive_sector_allocator::merge_from(
        utils::block_manager<sector_id> &other) noexcept -> result<void>
    {
        return mSectorManager.merge_from(other);
    }
    auto archive_sector_allocator::merge_disjunct(
        utils::block_manager<sector_id> &other) noexcept -> result<void>
    {
        return mSectorManager.merge_disjunct(other);
    }

    auto archive_sector_allocator::mine_new_raw(int num) noexcept
        -> result<utils::id_range<sector_id>>
    {
        assert(num > 0);
        using id_range_t = utils::id_range<sector_id>;

        auto oldSize = mSectorDevice.size();
        if (auto resizerx = mSectorDevice.resize(oldSize + num); !resizerx)
        {
            return error(errc::resource_exhausted)
                   << ed::wrapped_error(std::move(resizerx).assume_error());
        }
        sector_id first{oldSize};
        return id_range_t{first, id_range_t::advance(first, num)};
    }

    auto archive_sector_allocator::mine_new(int num) noexcept -> result<void>
    {
        assert(num > 0);

        VEFS_TRY(allocated, mine_new_raw(num));

        if (auto insertrx =
                mSectorManager.dealloc_contiguous(allocated.first(), num);
            !insertrx)
        {
            if (auto shrinkrx = mSectorDevice.resize(
                    static_cast<uint64_t>(allocated.first()));
                shrinkrx.has_failure())
            {
                // can't keep track of the newly allocated sectors
                // neither the manager had space nor could we deallocate
                // them, therefore we leak them until the recovery is
                // invoked
                sectors_leaked();

                shrinkrx.assume_error()
                    << ed::wrapped_error(std::move(insertrx).assume_error());
                return std::move(shrinkrx).assume_error();
            }
            return std::move(insertrx).as_failure();
        }
        return success();
    }
} // namespace vefs::detail
