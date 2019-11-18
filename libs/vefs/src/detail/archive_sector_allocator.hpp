#pragma once

#include <mutex>

#include "block_manager.hpp"
#include "sector_device.hpp"

namespace vefs::detail
{
    class archive_sector_allocator
    {
    public:
        auto alloc_one() noexcept -> result<sector_id>
        {
            std::lock_guard allocGuard{mAllocatorSync};

            if (auto allocationrx = mSectorManager.alloc_one();
                allocationrx ||
                allocationrx.assume_error() != errc::resource_exhausted)
            {
                return allocationrx;
            }

            auto oldSize = mSectorDevice.size();
            if (auto resizerx = mSectorDevice.resize(oldSize + 4); !resizerx)
            {
                return error(errc::resource_exhausted)
                       << ed::wrapped_error(std::move(resizerx).assume_error());
            }

            sector_id allocated{oldSize};
            if (auto insertrx = mSectorManager.dealloc_contiguous(
                    sector_id{oldSize + 1}, 3);
                !insertrx)
            {
                if (auto shrinkrx = mSectorDevice.resize(oldSize + 1);
                    !shrinkrx)
                {
                    // can't keep track of the newly allocated sectors
                    // neither the manager had space nor could we deallocate
                    // them, therefore we leak them until the recovery is
                    // invoked
                    sectors_leaked();

                    shrinkrx.assume_error() << ed::wrapped_error(
                        std::move(insertrx).assume_error());
                    return std::move(shrinkrx).assume_error();
                }
            }
            return allocated;
        }

        void dealloc_one(sector_id which) noexcept
        {
            if (!mSectorManager.dealloc_one(which))
            {
                sectors_leaked();
            }
        }

    private:
        void sectors_leaked();

        sector_device &mSectorDevice;
        utils::block_manager<sector_id> mSectorManager;
        std::mutex mAllocatorSync;
    };
} // namespace vefs::detail
