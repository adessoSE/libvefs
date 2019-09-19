#pragma once

#include <memory>
#include <mutex>
#include <shared_mutex>

#include <vefs/platform/platform.hpp>

#include <boost/container/static_vector.hpp>

#include "cache_car.hpp"
#include "file_crypto_ctx.hpp"
#include "root_sector_info.hpp"
#include "sector.hpp"
#include "tree_walker.hpp"

namespace vefs::detail
{
    class test_allocator
    {
    };

    template <typename SectorAllocator>
    class file_mt
    {
    public:
        using sector_allocator_type = SectorAllocator;

        template <typename T>
        class sector_policy
        {
        public:
            using handle_type = cache_handle<T>;

            inline auto parent() const noexcept -> const handle_type &;
            inline void parent(handle_type newParent) noexcept;

        protected:
            inline sector_policy(handle_type parent);

            inline static auto is_dirty(const handle_type &h) noexcept;
            inline static void mark_dirty(handle_type &h) noexcept;
            inline static void mark_clean(handle_type &h) noexcept;

            inline auto reallocate(sector_id current) noexcept -> result<sector_id>;
            inline void deallocate(sector_id id) noexcept;

            inline void lock();
            inline auto try_lock() -> bool;
            inline void unlock();
            inline void lock_shared();
            inline auto try_lock_shared() -> bool;
            inline void unlock_shared();

        private:
            handle_type mParent;
            std::shared_mutex mSectorSync;
        };

        using sector_type = basic_sector<sector_policy>;
        using sector_handle = typename sector_type::handle_type;
        using sector_cache = cache_car<tree_position, sector_type, 1 << 6>; // 64 cached pages

        file_mt(sector_device &device, file_crypto_ctx &cryptoCtx, root_sector_info rootInfo);

        auto access(tree_position sectorPosition) -> result<sector_handle>;

    private:
        template <bool ReturnParentIfNotAllocated>
        auto access(tree_position sectorPosition) -> result<sector_handle>;

        auto adjust_tree_depth(int targetDepth) noexcept -> result<void>;
        auto increase_tree_depth(int targetDepth) noexcept -> result<void>;
        auto decrease_tree_depth(int targetDepth) noexcept -> result<void>;

        auto access_or_read_child(sector_handle parent, tree_position childPosition,
                                  int childParentOffset) noexcept -> result<sector_handle>;

        sector_device &mDevice;
        file_crypto_ctx &mCryptoCtx;

        std::unique_ptr<sector_cache> mSectorCache;
        std::mutex mTreeDepthSync;
        sector_handle mRootSector;
        root_sector_info mRootInfo;
        root_sector_info mNextRootInfo;
    };

#pragma region policy implementation

    template <typename SectorAllocator>
    template <typename T>
    inline file_mt<SectorAllocator>::sector_policy<T>::sector_policy(handle_type parent)
        : mParent(std::move(parent))
    {
    }
    template <typename SectorAllocator>
    template <typename T>
    inline auto file_mt<SectorAllocator>::sector_policy<T>::parent() const noexcept
        -> const handle_type &
    {
        return mParent;
    }
    template <typename SectorAllocator>
    template <typename T>
    inline void file_mt<SectorAllocator>::sector_policy<T>::parent(handle_type newParent) noexcept
    {
        mParent = std::move(newParent);
    }
    template <typename SectorAllocator>
    template <typename T>
    inline auto file_mt<SectorAllocator>::sector_policy<T>::is_dirty(const handle_type &h) noexcept
    {
        return h.is_dirty();
    }
    template <typename SectorAllocator>
    template <typename T>
    inline void file_mt<SectorAllocator>::sector_policy<T>::mark_dirty(handle_type &h) noexcept
    {
        h.mark_dirty();
    }
    template <typename SectorAllocator>
    template <typename T>
    inline void file_mt<SectorAllocator>::sector_policy<T>::mark_clean(handle_type &h) noexcept
    {
        h.mark_clean();
    }
    template <typename SectorAllocator>
    template <typename T>
    inline auto file_mt<SectorAllocator>::sector_policy<T>::reallocate(sector_id current) noexcept
        -> result<sector_id>
    {
        return current;
    }
    template <typename SectorAllocator>
    template <typename T>
    inline void file_mt<SectorAllocator>::sector_policy<T>::deallocate(sector_id id) noexcept
    {
    }
    template <typename SectorAllocator>
    template <typename T>
    inline void file_mt<SectorAllocator>::sector_policy<T>::lock()
    {
        mSectorSync.lock();
    }
    template <typename SectorAllocator>
    template <typename T>
    inline auto file_mt<SectorAllocator>::sector_policy<T>::try_lock() -> bool
    {
        return mSectorSync.try_lock();
    }
    template <typename SectorAllocator>
    template <typename T>
    inline void file_mt<SectorAllocator>::sector_policy<T>::unlock()
    {
        mSectorSync.unlock();
    }
    template <typename SectorAllocator>
    template <typename T>
    inline void file_mt<SectorAllocator>::sector_policy<T>::lock_shared()
    {
        mSectorSync.lock_shared();
    }
    template <typename SectorAllocator>
    template <typename T>
    inline auto file_mt<SectorAllocator>::sector_policy<T>::try_lock_shared() -> bool
    {
        return mSectorSync.try_lock_shared();
    }
    template <typename SectorAllocator>
    template <typename T>
    inline void file_mt<SectorAllocator>::sector_policy<T>::unlock_shared()
    {
        mSectorSync.unlock_shared();
    }

#pragma endregion

#pragma region file_mt implementation

    template <typename SectorAllocator>
    inline auto file_mt<SectorAllocator>::access(tree_position logicalPosition)
        -> result<sector_handle>
    {
        return access<false>(logicalPosition);
    }

    template <typename SectorAllocator>
    template <bool ReturnParentIfNotAllocated>
    inline auto file_mt<SectorAllocator>::access(tree_position logicalPosition)
        -> result<sector_handle>
    {
        tree_path path{5, logicalPosition};

        sector_handle base;
        auto it = std::find_if(path.rbegin(), path.rend(), [this, &base](tree_position position) {
                      return base = mSectorCache->try_access(position);
                  }).base();

        // current root is always in cache, i.e. if nothing is hit, it's out of range
        if (!base)
        {
            return archive_errc::sector_reference_out_of_range;
        }

        // next sector is unlikely to be in the page cache,
        // therefore it is even more unlikely that its reference
        // resides in the CPU cache
        // however this only holds for the first reference load,
        // because afterwards the freshly decrypted sector content
        // will still reside in cache
        VEFS_PREFETCH_NTA(as_span(*base).data() +
                          it.array_offset() * reference_sector_layout::serialized_reference_size);

        for (const auto end = path.end(); it != end; ++it)
        {
            // we only need to increment the cache ref ctr twice in case we need it for the not
            // allocated case
            [[maybe_unused]] sector_handle parentBackup;
            if constexpr (ReturnParentIfNotAllocated)
            {
                parentBackup = base;
            }

            if (auto entry = access_or_read_child(std::move(base), *it, it.array_offset()))
            {
                base = std::move(entry).assume_value();
            }
            else
            {
                if constexpr (ReturnParentIfNotAllocated)
                {
                    if (entry.assume_error() == archive_errc::sector_reference_out_of_range)
                    {
                        return std::move(parentBackup);
                    }
                }
                return std::move(entry).as_failure();
            }
        }
        return std::move(base);
    }

    template <typename SectorAllocator>
    inline auto file_mt<SectorAllocator>::adjust_tree_depth(int targetDepth) noexcept
        -> result<void>
    {
        std::lock_guard treeDepthLock{mTreeDepthSync};

        if (mNextRootInfo.tree_depth < targetDepth)
        {
            return increase_tree_depth(targetDepth);
        }
        else if (mNextRootInfo.tree_depth > targetDepth)
        {
            // shrink
            return errc::not_supported;
        }
        return success();
    }

    template <typename SectorAllocator>
    inline auto file_mt<SectorAllocator>::increase_tree_depth(const int targetDepth) noexcept
        -> result<void>
    {
        using boost::container::static_vector;

        const int depthDifference = targetDepth - mNextRootInfo.tree_depth;

        static_vector<sector_id, lut::max_tree_depth + 1> allocatedSectors;
        utils::scope_guard allocationRollbackGuard = [this, &allocatedSectors]() {
            for (auto it = allocatedSectors.begin(), end = allocatedSectors.end(); it != end; ++it)
            {
                // #TODO deallocate
            }
        };

        // we allocate the required disc space before making any changes,
        // because it is the only thing that can fail
        allocatedSectors.resize(depthDifference);
        // mSectorAllocator.alloc_range(allocatedSectors);

        // we grow bottom to top in order to not disturb any ongoing access
        for (int i = mNextRootInfo.tree_depth; i < targetDepth; ++i)
        {
            auto physicalPosition = allocatedSectors.back();
            allocatedSectors.pop_back();
            tree_position nextRootPos(0u, i);

            std::unique_lock oldRootLock{*mRootSector};
            auto createrx = mSectorCache->access(
                nextRootPos, [ this, nextRootPos,
                               physicalPosition ](void *mem) noexcept->result<sector_type *, void> {
                    auto xsec = new (mem) sector_type(nextRootPos, physicalPosition, mRootSector);
                    // zero out the sector content
                    auto content = as_span(*xsec);
                    reference_sector_layout{content}.write(0, mNextRootInfo.root);
                    fill_blob(content.template subspan<
                              reference_sector_layout::serialized_reference_size>());

                    return xsec;
                });

            mRootSector->policy().parent(createrx.assume_value());
            mNextRootInfo.root = {};
            oldRootLock.unlock();
            mRootSector = std::move(createrx).assume_value();
        }
        return success();
    }

    template <typename SectorAllocator>
    inline auto file_mt<SectorAllocator>::decrease_tree_depth(int targetDepth) noexcept
        -> result<void>
    {
        return result<void>();
    }

    template <typename SectorAllocator>
    inline auto file_mt<SectorAllocator>::access_or_read_child(sector_handle parent,
                                                               tree_position childPosition,
                                                               int childParentOffset) noexcept
        -> result<sector_handle>
    {
        return mSectorCache->access(
            childPosition, [&](void *mem) noexcept->result<sector_type *> {
                const auto ref = reference_sector_layout{as_span(*parent)}.read(childParentOffset);

                if (ref.sector == sector_id::master)
                {
                    return archive_errc::sector_reference_out_of_range;
                }

                auto xsec = new (mem) sector_type(childPosition, ref.sector, std::move(parent));

                if (auto readResult =
                        mDevice.read_sector(as_span(*xsec), mCryptoCtx, ref.sector, ref.mac);
                    readResult.has_failure())
                {
                    std::destroy_at(xsec);
                    readResult.assume_error() << ed::sector_idx{ref.sector};
                    return std::move(readResult).as_failure();
                }
                return xsec;
            });
    }

#pragma endregion

    template class file_mt<test_allocator>;
} // namespace vefs::detail
