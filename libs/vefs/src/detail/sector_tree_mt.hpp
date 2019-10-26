#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>

#include <vefs/platform/platform.hpp>
#include <vefs/platform/thread_pool.hpp>

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
    public:
        auto alloc_one() noexcept -> result<sector_id>
        {
            return errc::resource_exhausted;
        }
        auto alloc_multiple(span<sector_id> ids) noexcept -> result<std::size_t>
        {
            return 0;
        }

        auto dealloc_one(const sector_id which) noexcept -> result<void>
        {
            return success();
        }

        auto on_commit() noexcept -> result<void>
        {
        }
    };

    template <typename SectorAllocator, typename Executor>
    class sector_tree_mt
    {
    public:
        using sector_allocator_type = SectorAllocator;
        using executor_type = Executor;

        template <typename T>
        class sector_policy
        {
        public:
            using handle_type = cache_handle<T>;

            /**
             * retrieves a handle to the parent
             * the handle will be empty if this is the root sector
             */
            inline auto parent() const noexcept -> const handle_type &;
            /**
             * updates the parent sector reference
             */
            inline void parent(handle_type newParent) noexcept;

        protected:
            inline sector_policy(sector_tree_mt &tree, handle_type parent);

            inline static auto is_dirty(const handle_type &h) noexcept;
            inline static void mark_dirty(handle_type &h) noexcept;
            inline static void mark_clean(handle_type &h) noexcept;

            inline auto reallocate(sector_id current) noexcept
                -> result<sector_id>;
            inline void deallocate(sector_id id) noexcept;

            inline void sync_succeeded(sector_reference ref) noexcept;

            inline void lock();
            inline auto try_lock() -> bool;
            inline void unlock();
            inline void lock_shared();
            inline auto try_lock_shared() -> bool;
            inline void unlock_shared();

        private:
            sector_tree_mt &mTree;
            sector_id mPreviousId;
            handle_type mParent;
            std::shared_mutex mSectorSync;
        };

        using sector_type = basic_sector<sector_policy>;
        using sector_handle = typename sector_type::handle_type;
        using sector_cache =
            cache_car<tree_position, sector_type, 1 << 6>; // 64 cached pages

    private:
        template <typename... AllocatorCtorArgs>
        sector_tree_mt(sector_device &device, file_crypto_ctx &cryptoCtx,
                       executor_type &executor, root_sector_info rootInfo,
                       AllocatorCtorArgs &&... args);

    public:
        template <typename... AllocatorCtorArgs>
        static auto
        open_existing(sector_device &device, file_crypto_ctx &cryptoCtx,
                      executor_type &executor, root_sector_info rootInfo,
                      AllocatorCtorArgs &&... args)
            -> result<std::unique_ptr<sector_tree_mt>>;

        /**
         * Tries to access from or load into cache the sector at the given node
         * position. Fails if the sector is not allocated.
         */
        auto access(tree_position node) -> result<sector_handle>;
        /**
         * Tries to access the sector at the given node position and creates
         * said sector if it doesn't exist.
         */
        auto access_or_create(tree_position node) -> result<sector_handle>;
        /**
         * Erase a leaf node at the given position.
         */
        auto erase_leaf(std::uint64_t leafId) -> result<void>;

        /**
         * Forces all cached information to be written to disc.
         */
        auto commit() -> result<void>;

    private:
        template <bool ReturnParentIfNotAllocated>
        auto access(tree_path::const_iterator pathBegin,
                    tree_path::const_iterator pathEnd) -> result<sector_handle>;

        void notify_dirty(sector_handle h) noexcept;

        auto adjust_tree_depth(int targetDepth) noexcept -> result<void>;
        auto increase_tree_depth(int targetDepth) noexcept -> result<void>;
        auto decrease_tree_depth(int targetDepth) noexcept -> result<void>;

        auto access_or_read_child(sector_handle parent,
                                  tree_position childPosition,
                                  int childParentOffset) noexcept
            -> result<sector_handle>;
        auto access_or_create_child(sector_handle parent,
                                    tree_position childPosition,
                                    int childParentOffset,
                                    sector_id sectorId) noexcept
            -> result<sector_handle>;
        auto try_erase_child(const sector_handle &parent, tree_position child,
                             int childParentOffset) noexcept -> result<bool>;
        auto erase_child(sector_handle parent, tree_position child,
                         int childParentOffset) noexcept -> result<void>;

        sector_device &mDevice;
        file_crypto_ctx &mCryptoCtx;
        executor_type &mExecutor;

        std::unique_ptr<sector_cache> mSectorCache;
        std::mutex mTreeDepthSync;
        sector_handle mRootSector;
        root_sector_info mRootInfo;

        sector_allocator_type mSectorAllocator;
    };

#pragma region policy implementation

    template <typename SectorAllocator, typename Executor>
    template <typename T>
    inline sector_tree_mt<SectorAllocator, Executor>::sector_policy<
        T>::sector_policy(sector_tree_mt &tree, handle_type parent)
        : mTree(tree)
        , mParent(std::move(parent))
    {
    }
    template <typename SectorAllocator, typename Executor>
    template <typename T>
    inline auto
    sector_tree_mt<SectorAllocator, Executor>::sector_policy<T>::parent() const
        noexcept -> const handle_type &
    {
        return mParent;
    }
    template <typename SectorAllocator, typename Executor>
    template <typename T>
    inline void
    sector_tree_mt<SectorAllocator, Executor>::sector_policy<T>::parent(
        handle_type newParent) noexcept
    {
        mParent = std::move(newParent);
    }
    template <typename SectorAllocator, typename Executor>
    template <typename T>
    inline auto
    sector_tree_mt<SectorAllocator, Executor>::sector_policy<T>::is_dirty(
        const handle_type &h) noexcept
    {
        return h.is_dirty();
    }
    template <typename SectorAllocator, typename Executor>
    template <typename T>
    inline void
    sector_tree_mt<SectorAllocator, Executor>::sector_policy<T>::mark_dirty(
        handle_type &h) noexcept
    {
        h.mark_dirty();
    }
    template <typename SectorAllocator, typename Executor>
    template <typename T>
    inline void
    sector_tree_mt<SectorAllocator, Executor>::sector_policy<T>::mark_clean(
        handle_type &h) noexcept
    {
        h.mark_clean();
    }
    template <typename SectorAllocator, typename Executor>
    template <typename T>
    inline auto
    sector_tree_mt<SectorAllocator, Executor>::sector_policy<T>::reallocate(
        sector_id current) noexcept -> result<sector_id>
    {
        return current;
    }
    template <typename SectorAllocator, typename Executor>
    template <typename T>
    inline void
    sector_tree_mt<SectorAllocator, Executor>::sector_policy<T>::deallocate(
        sector_id id) noexcept
    {
    }
    template <typename SectorAllocator, typename Executor>
    template <typename T>
    inline void
    sector_tree_mt<SectorAllocator, Executor>::sector_policy<T>::sync_succeeded(
        sector_reference ref) noexcept
    {
        if (!parent())
        {
            // the root sector is only synced during commit,
            // therefore a locked depth sync is guaranteed
            mTree.mRootInfo.root = ref;
        }
    }
    template <typename SectorAllocator, typename Executor>
    template <typename T>
    inline void
    sector_tree_mt<SectorAllocator, Executor>::sector_policy<T>::lock()
    {
        mSectorSync.lock();
    }
    template <typename SectorAllocator, typename Executor>
    template <typename T>
    inline auto
    sector_tree_mt<SectorAllocator, Executor>::sector_policy<T>::try_lock()
        -> bool
    {
        return mSectorSync.try_lock();
    }
    template <typename SectorAllocator, typename Executor>
    template <typename T>
    inline void
    sector_tree_mt<SectorAllocator, Executor>::sector_policy<T>::unlock()
    {
        mSectorSync.unlock();
    }
    template <typename SectorAllocator, typename Executor>
    template <typename T>
    inline void
    sector_tree_mt<SectorAllocator, Executor>::sector_policy<T>::lock_shared()
    {
        mSectorSync.lock_shared();
    }
    template <typename SectorAllocator, typename Executor>
    template <typename T>
    inline auto sector_tree_mt<SectorAllocator,
                               Executor>::sector_policy<T>::try_lock_shared()
        -> bool
    {
        return mSectorSync.try_lock_shared();
    }
    template <typename SectorAllocator, typename Executor>
    template <typename T>
    inline void
    sector_tree_mt<SectorAllocator, Executor>::sector_policy<T>::unlock_shared()
    {
        mSectorSync.unlock_shared();
    }

#pragma endregion

#pragma region sector_tree_mt implementation

    template <typename SectorAllocator, typename Executor>
    template <typename... AllocatorCtorArgs>
    inline sector_tree_mt<SectorAllocator, Executor>::sector_tree_mt(
        sector_device &device, file_crypto_ctx &cryptoCtx,
        executor_type &executor, root_sector_info rootInfo,
        AllocatorCtorArgs &&... allocatorCtorArgs)
        : mDevice(device)
        , mCryptoCtx(cryptoCtx)
        , mExecutor(executor)
        , mSectorCache(std::make_unique<sector_cache>(
              [this](sector_handle h) { notify_dirty(std::move(h)); }))
        , mTreeDepthSync()
        , mRootSector()
        , mRootInfo(rootInfo)
        , mSectorAllocator(
              std::forward<AllocatorCtorArgs>(allocatorCtorArgs)...)
    {
        // #TODO load root sector from disc
    }

    template <typename SectorAllocator, typename Executor>
    inline auto sector_tree_mt<SectorAllocator, Executor>::access(
        tree_position nodePosition) -> result<sector_handle>
    {
        const tree_path accessPath(5, nodePosition);
        return access<false>(accessPath.begin(), accessPath.end());
    }

    template <typename SectorAllocator, typename Executor>
    inline auto sector_tree_mt<SectorAllocator, Executor>::access_or_create(
        tree_position node) -> result<sector_handle>
    {
        using boost::container::static_vector;

        tree_path sectorPath{5, node};
        int requiredDepth = 0;
        for (; sectorPath.position(requiredDepth) != 0; ++requiredDepth)
        {
        }
        {
            std::lock_guard treeDepthLock{mTreeDepthSync};
            if (mRootInfo.tree_depth < requiredDepth)
            {
                VEFS_TRY(increase_tree_depth(requiredDepth));
            }
        }

        sector_handle mountPoint;
        if (auto sectorrx = access<true>(sectorPath.begin(), sectorPath.end());
            !sectorrx || sectorrx.assume_value()->node_position() == node)
        {
            return std::move(sectorrx);
        }
        else
        {
            mountPoint = std::move(sectorrx).assume_value();
        }

        int missingLayers = mountPoint->node_position().layer() - node.layer();
        static_vector<sector_id, lut::max_tree_depth> allocatedSectors;
        utils::scope_guard allocationRollbackGuard = [&]() {
            for (auto it = allocatedSectors.begin(),
                      end = allocatedSectors.end();
                 it != end; ++it)
            {
                (void)mSectorAllocator.dealloc_one(*it);
            }
        };

        // we allocate the required disc space before making any changes,
        // because it is the only thing that can still fail
        allocatedSectors.resize(missingLayers);
        VEFS_TRY(mSectorAllocator.alloc_multiple(allocatedSectors));

        for (auto it = tree_path::iterator(
                      sectorPath, mountPoint->node_position().layer() - 1),
                  end = sectorPath.end();
             it != end; ++it)
        {
            const auto nodePos = *it;
            const auto sectorId = allocatedSectors.back();
            allocatedSectors.pop_back();

            if (auto rx = access_or_create_child(std::move(mountPoint), nodePos,
                                                 it.array_offset(), sectorId))
            {
                mountPoint = std::move(rx).assume_value();
            }
            else
            {
                return std::move(rx).as_failure();
            }
        }
        return std::move(mountPoint);
    }

    template <typename SectorAllocator, typename Executor>
    inline auto
    sector_tree_mt<SectorAllocator, Executor>::erase_leaf(std::uint64_t leafId)
        -> result<void>
    {
        const tree_position leafPos(leafId, 0);
        const tree_path leafPath(5, leafPos);

        sector_handle leafParent;
        if (auto accessrx =
                access<false>(leafPath.cbegin(), std::prev(leafPath.cend())))
        {
            leafParent = std::move(accessrx).assume_value();
        }
        else if (accessrx.assume_error() ==
                 archive_errc::sector_reference_out_of_range)
        {
            // leaf parent not allocated => child not allocated
            return success();
        }
        else
        {
            return std::move(accessrx).as_failure();
        }

        return erase_child(std::move(leafParent), leafPos, leafPath.offset(0));
    }

    template <typename SectorAllocator, typename Executor>
    inline auto sector_tree_mt<SectorAllocator, Executor>::commit()
        -> result<void>
    {
        std::lock_guard depthLock{mTreeDepthSync};

        bool anyDirty = true;
        for (int i = 0; anyDirty && i <= mRootInfo.tree_depth; ++i)
        {
            auto rx = mSectorCache->for_dirty(
                [this, i](sector_handle node) -> result<void> {
                    if (node->node_position().layer() != i)
                    {
                        return success();
                    }
                    std::lock_guard nodeLock{*node};
                    if (!node.is_dirty())
                    {
                        return success();
                    }

                    VEFS_TRY(node->sync_to(mDevice, mCryptoCtx, node));
                });

            if (rx)
            {
                anyDirty = rx.assume_value();
            }
            else
            {
                return std::move(rx).as_failure();
            }
        }

        VEFS_TRY(mSectorAllocator.on_commit());

        return success();
    }

    template <typename SectorAllocator, typename Executor>
    template <bool ReturnParentIfNotAllocated>
    inline auto sector_tree_mt<SectorAllocator, Executor>::access(
        tree_path::const_iterator pathBegin, tree_path::const_iterator pathEnd)
        -> result<sector_handle>
    {
        sector_handle base;
        auto it =
            std::find_if(std::make_reverse_iterator(pathEnd),
                         std::make_reverse_iterator(pathBegin),
                         [this, &base](tree_position position) {
                             return (base = mSectorCache->try_access(position));
                         })
                .base();

        // current root is always in cache, i.e. if nothing is hit, it's out of
        // range
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
        VEFS_PREFETCH_NTA(
            as_span(*base).data() +
            it.array_offset() *
                reference_sector_layout::serialized_reference_size);

        for (; it != pathEnd; ++it)
        {
            // we only need to increment the cache ref ctr twice in case we need
            // it for the not allocated case
            [[maybe_unused]] sector_handle parentBackup;
            if constexpr (ReturnParentIfNotAllocated)
            {
                parentBackup = base;
            }

            if (auto entry = access_or_read_child(std::move(base), *it,
                                                  it.array_offset()))
            {
                base = std::move(entry).assume_value();
            }
            else
            {
                if constexpr (ReturnParentIfNotAllocated)
                {
                    if (entry.assume_error() ==
                        archive_errc::sector_reference_out_of_range)
                    {
                        return std::move(parentBackup);
                    }
                }
                return std::move(entry).as_failure();
            }
        }
        return std::move(base);
    }

    template <typename SectorAllocator, typename Executor>
    inline void sector_tree_mt<SectorAllocator, Executor>::notify_dirty(
        sector_handle h) noexcept
    {
        // #TODO package to executor
        mExecutor.execute([this, h = std::move(h)]() mutable {
            if (!h)
            {
                return;
            }

            std::unique_lock sectorLock{*h};
            if (!h.is_dirty())
            {
                return;
            }

            if (h->node_position().layer() > 0 &&
                h->node_position().position() != 0)
            {
                auto sector = as_span(*h);
                if (std::all_of(sector.begin(), sector.end(),
                                [](std::byte b) { return b == std::byte{}; }))
                {
                    // empty reference sector
                    auto parent = h->policy().parent();
                    auto position = h->node_position();
                    auto childOffset = position.parent_array_offset();
                    h.mark_clean();

                    sectorLock.unlock();
                    h = nullptr;

                    // maybe refactor erase_child to return
                    // a handle to the child on erasure failure
                    (void)try_erase_child(std::move(parent), position,
                                          childOffset);
                    return;
                }
            }
            h->sync_to(mDevice, mCryptoCtx, h);
        });
    }

    template <typename SectorAllocator, typename Executor>
    inline auto sector_tree_mt<SectorAllocator, Executor>::adjust_tree_depth(
        int targetDepth) noexcept -> result<void>
    {
        // usefulness of this method still needs to be determined
        std::lock_guard treeDepthLock{mTreeDepthSync};

        if (mRootInfo.tree_depth < targetDepth)
        {
            return increase_tree_depth(targetDepth);
        }
        else if (mRootInfo.tree_depth > targetDepth)
        {
            return decrease_tree_depth(targetDepth);
        }
        return success();
    }

    template <typename SectorAllocator, typename Executor>
    inline auto sector_tree_mt<SectorAllocator, Executor>::increase_tree_depth(
        const int targetDepth) noexcept -> result<void>
    {
        using boost::container::static_vector;

        const int depthDifference = targetDepth - mRootInfo.tree_depth;

        static_vector<sector_id, lut::max_tree_depth + 1> allocatedSectors;
        utils::scope_guard allocationRollbackGuard = [this,
                                                      &allocatedSectors]() {
            for (auto it = allocatedSectors.begin(),
                      end = allocatedSectors.end();
                 it != end; ++it)
            {
                (void)mSectorAllocator.dealloc_one(*it);
            }
        };

        // we allocate the required disc space before making any changes,
        // because it is the only thing that can fail
        allocatedSectors.resize(depthDifference);
        VEFS_TRY(mSectorAllocator.alloc_multiple(allocatedSectors));

        // we grow bottom to top in order to not disturb any ongoing access
        for (int i = mRootInfo.tree_depth; i < targetDepth; ++i)
        {
            auto sectorId = allocatedSectors.back();
            allocatedSectors.pop_back();
            tree_position nextRootPos(0u, i);

            std::unique_lock oldRootLock{*mRootSector};
            auto createrx = mSectorCache->access(
                nextRootPos,
                [ this, nextRootPos,
                  sectorId ](void *mem) noexcept->result<sector_type *, void> {
                    auto xsec = new (mem)
                        sector_type(nextRootPos, sectorId, mRootSector);
                    // zero out the sector content
                    auto content = as_span(*xsec);
                    reference_sector_layout{content}.write(0, mRootInfo.root);
                    fill_blob(
                        content
                            .template subspan<reference_sector_layout::
                                                  serialized_reference_size>());

                    return xsec;
                });
            auto root = std::move(createrx).assume_value();

            mRootSector->policy().parent(root);
            mRootInfo.root = {};
            oldRootLock.unlock();
            mRootSector = std::move(root);
        }
        mRootInfo.tree_depth = targetDepth;
        return success();
    }

    template <typename SectorAllocator, typename Executor>
    inline auto sector_tree_mt<SectorAllocator, Executor>::decrease_tree_depth(
        int targetDepth) noexcept -> result<void>
    {
        using boost::container::static_vector;

        VEFS_TRY(newRoot, access(tree_position(0, targetDepth)));

        static_vector<sector_handle, lut::max_tree_depth + 1> victimChildren;

        for (sector_handle child = newRoot, parent = newRoot->policy().parent();
             parent; parent = child->policy().parent())
        {
            victimChildren.emplace_back(
                std::exchange(child, std::move(parent)));
        }

        std::for_each(
            victimChildren.rbegin(), victimChildren.rend(),
            [this](sector_handle &current) {
                std::unique_lock xguard{*current};
                auto parent = current->policy().parent();
                current->policy().parent(sector_handle{});
                auto sectorId = parent->sector_id();
                for (;;)
                {
                    mRootInfo.root =
                        reference_sector_layout{as_span(*parent)}.read(0);
                    mSectorCache->try_purge(parent);
                    xguard.unlock();
                    if (!parent)
                    {
                        break;
                    }
                    std::this_thread::yield();
                    xguard.lock();
                }
                (void)mSectorAllocator.dealloc_one(sectorId);
                current = sector_handle{};
            });

        mRootInfo.tree_depth = targetDepth;
        return success();
    }

    template <typename SectorAllocator, typename Executor>
    inline auto sector_tree_mt<SectorAllocator, Executor>::access_or_read_child(
        sector_handle parent, tree_position childPosition,
        int childParentOffset) noexcept -> result<sector_handle>
    {
        return mSectorCache->access(
            childPosition, [&](void *mem) noexcept->result<sector_type *> {
                const auto ref = reference_sector_layout{as_span(*parent)}.read(
                    childParentOffset);

                if (ref.sector == sector_id::master)
                {
                    return archive_errc::sector_reference_out_of_range;
                }

                auto xsec = new (mem)
                    sector_type(childPosition, ref.sector, std::move(parent));

                if (auto readResult = mDevice.read_sector(
                        as_span(*xsec), mCryptoCtx, ref.sector, ref.mac);
                    readResult.has_failure())
                {
                    std::destroy_at(xsec);
                    readResult.assume_error() << ed::sector_idx{ref.sector};
                    return std::move(readResult).as_failure();
                }
                return xsec;
            });
    }

    template <typename SectorAllocator, typename Executor>
    inline auto
    sector_tree_mt<SectorAllocator, Executor>::access_or_create_child(
        sector_handle parent, tree_position childPosition,
        int childParentOffset, sector_id childSectorId) noexcept
        -> result<sector_handle>
    {
        auto rx = mSectorCache->access(
            childPosition, [&](void *mem) noexcept->result<sector_type *> {
                auto ref = reference_sector_layout{as_span(*parent)}.read(
                    childParentOffset);
                if (ref.sector != sector_id::master)
                {
                    auto xsec = new (mem) sector_type(childPosition, ref.sector,
                                                      std::move(parent));

                    if (auto readResult = mDevice.read_sector(
                            as_span(*xsec), mCryptoCtx, ref.sector, ref.mac);
                        readResult.has_failure())
                    {
                        std::destroy_at(xsec);
                        readResult.assume_error() << ed::sector_idx{ref.sector};
                        return std::move(readResult).as_failure();
                    }
                    return xsec;
                }
                else
                {
                    auto xsec = new (mem) sector_type(
                        childPosition, childSectorId, std::move(parent));
                    childSectorId = sector_id::master;
                    return xsec;
                }
            });
        if (childSectorId != sector_id::master)
        {
            VEFS_TRY(mSectorAllocator.dealloc_one(childSectorId));
        }
        return success();
    }

    template <typename SectorAllocator, typename Executor>
    inline auto sector_tree_mt<SectorAllocator, Executor>::try_erase_child(
        const sector_handle &parent, tree_position child,
        int childParentOffset) noexcept -> result<bool>
    {
        sector_id childSectorId;
        if (mSectorCache->try_purge(child, [&]() {
                reference_sector_layout parentLayout{as_span(*parent)};
                childSectorId = parentLayout.read(childParentOffset).sector;
                parentLayout.write(childParentOffset, {sector_id::master, {}});
            }))
        {
            if (childSectorId != sector_id::master)
            {
                (void)mSectorAllocator.dealloc_one(childSectorId);
                VEFS_TRY(mDevice.erase_sector(childSectorId));
            }
            return true;
        }
        return false;
    }

    template <typename SectorAllocator, typename Executor>
    inline auto sector_tree_mt<SectorAllocator, Executor>::erase_child(
        sector_handle parent, tree_position child,
        int childParentOffset) noexcept -> result<void>
    {
        for (;;)
        {
            VEFS_TRY(erased, try_erase_child(parent, child, childParentOffset));
            if (erased)
            {
                return success();
            }

            std::this_thread::yield();
            if (auto h = mSectorCache->try_access(child))
            {
                h.mark_clean();
            }
        }
    }

#pragma endregion

    template class sector_tree_mt<test_allocator, thread_pool>;
} // namespace vefs::detail
