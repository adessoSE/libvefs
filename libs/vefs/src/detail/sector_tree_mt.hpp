#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <tuple>

#include <boost/container/static_vector.hpp>

#include <vefs/platform/platform.hpp>

#include "cache_car.hpp"
#include "file_crypto_ctx.hpp"
#include "reference_sector_layout.hpp"
#include "root_sector_info.hpp"
#include "tree_walker.hpp"

namespace vefs::detail
{
    template <typename TreeAllocator, typename Executor, typename MutexType = std::mutex>
    class sector_tree_mt
    {
    public:
        using tree_allocator_type = TreeAllocator;
        using node_allocator_type =
            typename tree_allocator_type::sector_allocator;
        using executor_type = Executor;

    private:
        class sector
        {
            friend class sector_tree_mt;

        public:
            using handle_type = cache_handle<sector>;

            sector(sector_tree_mt &tree, handle_type parent,
                   tree_position nodePosition, sector_id current) noexcept;

            auto node_position() const noexcept -> tree_position;

            /**
             * retrieves a handle to the parent
             * the handle will be empty if this is the root sector
             */
            inline auto parent() const noexcept -> const handle_type &;
            /**
             * updates the parent sector reference
             */
            inline void parent(handle_type newParent) noexcept;

            inline void lock();
            inline auto try_lock() -> bool;
            inline void unlock();
            inline void lock_shared();
            inline auto try_lock_shared() -> bool;
            inline void unlock_shared();

            inline friend auto as_span(sector &node) noexcept
                -> rw_blob<sector_device::sector_payload_size>
            {
                return node.mBlockData;
            }
            inline friend auto as_span(const sector &node) noexcept
                -> ro_blob<sector_device::sector_payload_size>
            {
                return node.mBlockData;
            }

        private:
            tree_position mNodePosition;
            sector_tree_mt &mTree;
            handle_type mParent;
            node_allocator_type mNodeAllocator;

            std::shared_mutex mSectorSync;

            std::array<std::byte, sector_device::sector_payload_size>
                mBlockData;
        };

        using sector_handle = cache_handle<sector>;

    public:
        class write_handle;

        class read_handle
        {
            friend class write_handle;

        public:
            read_handle() noexcept = default;
            read_handle(sector_handle node) noexcept;
            read_handle(write_handle const &writeHandle) noexcept;
            read_handle(write_handle &&writeHandle) noexcept;

            explicit operator bool() const noexcept;

            inline void lock();
            inline auto try_lock() -> bool;
            inline void unlock();
            inline void lock_shared();
            inline auto try_lock_shared() -> bool;
            inline void unlock_shared();

            inline auto node_position() noexcept -> tree_position;

            inline friend auto as_span(read_handle const &node) noexcept
                -> ro_blob<sector_device::sector_payload_size>
            {
                return as_span(*node.mSector);
            }

        private:
            sector_handle mSector;
        };

        class write_handle
        {
            friend class read_handle;

        public:
            write_handle() noexcept = default;
            write_handle(sector_handle node) noexcept;
            explicit write_handle(read_handle const &readHandle) noexcept;
            explicit write_handle(read_handle &&readHandle) noexcept;

            ~write_handle();

            explicit operator bool() const noexcept;

            inline void lock();
            inline auto try_lock() -> bool;
            inline void unlock();
            inline void lock_shared();
            inline auto try_lock_shared() -> bool;
            inline void unlock_shared();

            inline auto node_position() noexcept -> tree_position;

            inline friend auto as_span(write_handle const &node) noexcept
                -> rw_blob<sector_device::sector_payload_size>
            {
                return as_span(*node.mSector);
            }

        private:
            sector_handle mSector;
        };

    private:
        using sector_type = sector;
        using sector_cache =
            cache_car<tree_position, sector_type, 1 << 10>; // 1024 cached pages

        template <typename... AllocatorCtorArgs>
        sector_tree_mt(sector_device &device, file_crypto_ctx &cryptoCtx,
                       executor_type &executor, root_sector_info rootInfo,
                       AllocatorCtorArgs &&... args);

        auto init_existing() -> result<void>;
        auto create_new() -> result<void>;

    public:
        template <typename... AllocatorCtorArgs>
        static auto
        open_existing(sector_device &device, file_crypto_ctx &cryptoCtx,
                      executor_type &executor, root_sector_info rootInfo,
                      AllocatorCtorArgs &&... args)
            -> result<std::unique_ptr<sector_tree_mt>>;

        template <typename... AllocatorCtorArgs>
        static auto
        create_new(sector_device &device, file_crypto_ctx &cryptoCtx,
                   executor_type &executor, AllocatorCtorArgs &&... args)
            -> result<std::unique_ptr<sector_tree_mt>>;

        /**
         * Tries to access from or load into cache the sector at the given node
         * position. Fails if the sector is not allocated.
         */
        auto access(tree_position node) -> result<read_handle>;
        /**
         * Tries to access the sector at the given node position and creates
         * said sector if it doesn't exist.
         */
        auto access_or_create(tree_position node) -> result<read_handle>;
        /**
         * Erase a leaf node at the given position.
         */
        auto erase_leaf(std::uint64_t leafId) -> result<void>;

        /**
         * Forces all cached information to be written to disc.
         */
        template <typename CommitFn>
        auto commit(CommitFn &&commitFn) -> result<void>;

    private:
        template <bool ReturnParentIfNotAllocated>
        auto access(tree_path::const_iterator pathBegin,
                    tree_path::const_iterator pathEnd) -> result<sector_handle>;

        void notify_dirty(sector_handle h) noexcept;
        auto sync_to_device(sector_handle h) noexcept -> result<void>;

        auto adjust_tree_depth(int targetDepth) noexcept -> result<void>;
        auto increase_tree_depth(int targetDepth) noexcept -> result<void>;
        auto decrease_tree_depth(int targetDepth) noexcept -> result<void>;

        auto access_or_read_child(sector_handle parent,
                                  tree_position childPosition,
                                  int childParentOffset) noexcept
            -> result<sector_handle>;
        auto access_or_create_child(sector_handle parent,
                                    tree_position childPosition,
                                    int childParentOffset) noexcept
            -> result<sector_handle>;
        auto try_erase_child(sector_handle const &parent, tree_position child,
                             int childParentOffset) noexcept -> result<bool>;
        auto erase_child(sector_handle parent, tree_position child,
                         int childParentOffset) noexcept -> result<void>;

        sector_device &mDevice;
        file_crypto_ctx &mCryptoCtx;
        executor_type &mExecutor;

        MutexType mTreeDepthSync;
        root_sector_info mRootInfo;

        tree_allocator_type mTreeAllocator;
        sector_cache mSectorCache;
        sector_handle mRootSector; // needs to be destructed before mSectorCache
        std::shared_mutex mCommitSync;
    };

#pragma region sector implementation

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline sector_tree_mt<TreeAllocator, Executor, MutexType>::sector::sector(
        sector_tree_mt &tree, handle_type parent, tree_position nodePosition,
        sector_id current) noexcept
        : mNodePosition(nodePosition)
        , mTree(tree)
        , mParent(std::move(parent))
        , mNodeAllocator(mTree.mTreeAllocator, current)
        , mSectorSync()
    {
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto
    sector_tree_mt<TreeAllocator, Executor, MutexType>::sector::node_position() const
        noexcept -> tree_position
    {
        return mNodePosition;
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto sector_tree_mt<TreeAllocator, Executor, MutexType>::sector::parent() const
        noexcept -> handle_type const &
    {
        return mParent;
    }
    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline void sector_tree_mt<TreeAllocator, Executor, MutexType>::sector::parent(
        handle_type newParent) noexcept
    {
        mParent = std::move(newParent);
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline void sector_tree_mt<TreeAllocator, Executor, MutexType>::sector::lock()
    {
        mSectorSync.lock();
    }
    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto sector_tree_mt<TreeAllocator, Executor, MutexType>::sector::try_lock()
        -> bool
    {
        return mSectorSync.try_lock();
    }
    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline void sector_tree_mt<TreeAllocator, Executor, MutexType>::sector::unlock()
    {
        mSectorSync.unlock();
    }
    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline void sector_tree_mt<TreeAllocator, Executor, MutexType>::sector::lock_shared()
    {
        mSectorSync.lock_shared();
    }
    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto
    sector_tree_mt<TreeAllocator, Executor, MutexType>::sector::try_lock_shared() -> bool
    {
        return mSectorSync.try_lock_shared();
    }
    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline void sector_tree_mt<TreeAllocator, Executor, MutexType>::sector::unlock_shared()
    {
        mSectorSync.unlock_shared();
    }

#pragma endregion

#pragma region read_handle implementation

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline sector_tree_mt<TreeAllocator, Executor, MutexType>::read_handle::read_handle(
        sector_handle node) noexcept
        : mSector(std::move(node))
    {
    }
    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline sector_tree_mt<TreeAllocator, Executor, MutexType>::read_handle::read_handle(
        const write_handle &writeHandle) noexcept
        : mSector(writeHandle.mSector)
    {
    }
    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline sector_tree_mt<TreeAllocator, Executor, MutexType>::read_handle::read_handle(
        write_handle &&writeHandle) noexcept
        : mSector(std::exchange(writeHandle.mSector, nullptr))
    {
        if (mSector)
        {
            mSector.mark_dirty();
        }
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    sector_tree_mt<TreeAllocator, Executor, MutexType>::read_handle::operator bool() const
        noexcept
    {
        return mSector.operator bool();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto sector_tree_mt<TreeAllocator,
                               Executor, MutexType>::read_handle::node_position() noexcept
        -> tree_position
    {
        return mSector->node_position();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    void sector_tree_mt<TreeAllocator, Executor, MutexType>::read_handle::lock()
    {
        mSector->lock_shared();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    auto sector_tree_mt<TreeAllocator, Executor, MutexType>::read_handle::try_lock()
        -> bool
    {
        return mSector->try_lock_shared();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    void sector_tree_mt<TreeAllocator, Executor, MutexType>::read_handle::unlock()
    {
        mSector->unlock_shared();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    void sector_tree_mt<TreeAllocator, Executor, MutexType>::read_handle::lock_shared()
    {
        mSector->lock_shared();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    auto sector_tree_mt<TreeAllocator, Executor, MutexType>::read_handle::try_lock_shared()
        -> bool
    {
        return mSector->try_lock_shared();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    void sector_tree_mt<TreeAllocator, Executor, MutexType>::read_handle::unlock_shared()
    {
        mSector->unlock_shared();
    }

#pragma endregion

#pragma region write_handle implementation

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline sector_tree_mt<TreeAllocator, Executor, MutexType>::write_handle::write_handle(
        sector_handle node) noexcept
        : mSector(std::move(node))
    {
    }
    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline sector_tree_mt<TreeAllocator, Executor, MutexType>::write_handle::write_handle(
        const read_handle &writeHandle) noexcept
        : mSector(writeHandle.mSector)
    {
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline sector_tree_mt<TreeAllocator, Executor, MutexType>::write_handle::write_handle(
        read_handle &&readHandle) noexcept
        : mSector(std::exchange(readHandle.mSector, nullptr))
    {
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    sector_tree_mt<TreeAllocator, Executor, MutexType>::write_handle::operator bool() const
        noexcept
    {
        return mSector.operator bool();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto sector_tree_mt<TreeAllocator,
                               Executor, MutexType>::write_handle::node_position() noexcept
        -> tree_position
    {
        return mSector->node_position();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    void sector_tree_mt<TreeAllocator, Executor, MutexType>::write_handle::lock()
    {
        mSector->lock();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    auto sector_tree_mt<TreeAllocator, Executor, MutexType>::write_handle::try_lock()
        -> bool
    {
        return mSector->try_lock();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    void sector_tree_mt<TreeAllocator, Executor, MutexType>::write_handle::unlock()
    {
        mSector->unlock();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    void sector_tree_mt<TreeAllocator, Executor, MutexType>::write_handle::lock_shared()
    {
        mSector->lock_shared();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    auto
    sector_tree_mt<TreeAllocator, Executor, MutexType>::write_handle::try_lock_shared()
        -> bool
    {
        return mSector->try_lock_shared();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    void sector_tree_mt<TreeAllocator, Executor, MutexType>::write_handle::unlock_shared()
    {
        mSector->unlock_shared();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline sector_tree_mt<TreeAllocator,
                          Executor, MutexType>::write_handle::~write_handle()
    {
        if (mSector)
        {
            mSector.mark_dirty();
        }
    }

#pragma endregion

#pragma region sector_tree_mt implementation

    template <typename TreeAllocator, typename Executor, typename MutexType>
    template <typename... AllocatorCtorArgs>
    inline sector_tree_mt<TreeAllocator, Executor, MutexType>::sector_tree_mt(
        sector_device &device, file_crypto_ctx &cryptoCtx,
        executor_type &executor, root_sector_info rootInfo,
        AllocatorCtorArgs &&... allocatorCtorArgs)
        : mDevice(device)
        , mCryptoCtx(cryptoCtx)
        , mExecutor(executor)
        , mTreeDepthSync()
        , mRootInfo(rootInfo)
        , mTreeAllocator(std::forward<AllocatorCtorArgs>(allocatorCtorArgs)...)
        , mSectorCache([this](sector_handle h) { notify_dirty(std::move(h)); })
        , mRootSector()
    {
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto sector_tree_mt<TreeAllocator, Executor, MutexType>::init_existing()
        -> result<void>
    {
        tree_position rootPosition(0, mRootInfo.tree_depth);

        auto loadrx = mSectorCache.access(
            rootPosition,
            [ this, rootPosition ](void *mem) noexcept->result<sector_type *> {
                auto ptr = new (mem) sector_type(*this, nullptr, rootPosition,
                                                 mRootInfo.root.sector);

                if (auto readrx = mDevice.read_sector(as_span(*ptr), mCryptoCtx,
                                                      mRootInfo.root.sector,
                                                      mRootInfo.root.mac);
                    readrx.has_failure())
                {
                    std::destroy_at(ptr);
                    return std::move(readrx).as_failure();
                }
                return ptr;
            });
        if (loadrx.has_failure())
        {
            return std::move(loadrx).as_failure();
        }

        mRootSector = std::move(loadrx).assume_value();

        return success();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto sector_tree_mt<TreeAllocator, Executor, MutexType>::create_new()
        -> result<void>
    {
        tree_position rootPosition{0, 0};

        auto loadrx = mSectorCache.access(
            rootPosition, [ this, rootPosition ](
                              void *mem) noexcept->result<sector_type *, void> {
                auto xsec = new (mem)
                    sector_type(*this, nullptr, rootPosition, sector_id{});

                auto const content = as_span(*xsec);
                std::memset(content.data(), 0, content.size());

                return xsec;
            });
        mRootSector = std::move(loadrx).assume_value();
        mRootSector.mark_dirty();

        return success();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    template <typename... AllocatorCtorArgs>
    inline auto sector_tree_mt<TreeAllocator, Executor, MutexType>::open_existing(
        sector_device &device, file_crypto_ctx &cryptoCtx,
        executor_type &executor, root_sector_info rootInfo,
        AllocatorCtorArgs &&... args) -> result<std::unique_ptr<sector_tree_mt>>
    {
        std::unique_ptr<sector_tree_mt> tree(new (std::nothrow) sector_tree_mt(
            device, cryptoCtx, executor, rootInfo,
            std::forward<AllocatorCtorArgs>(args)...));
        if (!tree)
        {
            return errc::not_enough_memory;
        }

        VEFS_TRY(tree->init_existing());
        return success(std::move(tree));
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    template <typename... AllocatorCtorArgs>
    inline auto sector_tree_mt<TreeAllocator, Executor, MutexType>::create_new(
        sector_device &device, file_crypto_ctx &cryptoCtx,
        executor_type &executor, AllocatorCtorArgs &&... args)
        -> result<std::unique_ptr<sector_tree_mt>>
    {
        std::unique_ptr<sector_tree_mt> tree(new (std::nothrow) sector_tree_mt(
            device, cryptoCtx, executor, {},
            std::forward<AllocatorCtorArgs>(args)...));
        if (!tree)
        {
            return errc::not_enough_memory;
        }

        VEFS_TRY(tree->create_new());
        return success(std::move(tree));
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto
    sector_tree_mt<TreeAllocator, Executor, MutexType>::access(tree_position nodePosition)
        -> result<read_handle>
    {
        const tree_path accessPath(nodePosition);
        VEFS_TRY(auto &&node, access<false>(accessPath.begin(), accessPath.end()));
        return read_handle(std::move(node));
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto sector_tree_mt<TreeAllocator, Executor, MutexType>::access_or_create(
        tree_position node) -> result<read_handle>
    {
        using boost::container::static_vector;

        tree_path sectorPath{node};
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

        VEFS_TRY(auto &&mountPoint,
                 access<true>(sectorPath.begin(), sectorPath.end()));
        if (mountPoint->node_position() == node)
        {
            return read_handle(std::move(mountPoint));
        }

        for (auto it = tree_path::iterator(
                      sectorPath, mountPoint->node_position().layer() - 1),
                  end = sectorPath.end();
             it != end; ++it)
        {
            const auto nodePos = *it;

            if (auto rx = access_or_create_child(std::move(mountPoint), nodePos,
                                                 it.array_offset()))
            {
                mountPoint = std::move(rx).assume_value();
                mountPoint.mark_dirty();
            }
            else
            {
                return std::move(rx).as_failure();
            }
        }
        return read_handle(std::move(mountPoint));
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto
    sector_tree_mt<TreeAllocator, Executor, MutexType>::erase_leaf(std::uint64_t leafId)
        -> result<void>
    {
        if (leafId == 0)
        {
            return errc::not_supported;
        }

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

    template <typename TreeAllocator, typename Executor, typename MutexType>
    template <typename CommitFn>
    inline auto
    sector_tree_mt<TreeAllocator, Executor, MutexType>::commit(CommitFn &&commitFn)
        -> result<void>
    {
        std::scoped_lock depthLock{mCommitSync, mTreeDepthSync};

        bool anyDirty = true;
        for (int i = 0; anyDirty && i <= mRootInfo.tree_depth; ++i)
        {
            auto rx = mSectorCache.for_dirty(
                [ this, i ](sector_handle node) noexcept->result<void> {
                    if (node->node_position().layer() != i)
                    {
                        return success();
                    }
                    std::lock_guard nodeLock{*node};
                    return sync_to_device(std::move(node));
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

        if (mRootInfo.tree_depth > 0)
        {
            // #TODO shrink tree height to fit

            // sector at position 0 is mandatory
            constexpr auto serialized_reference_size =
                reference_sector_layout::serialized_reference_size;
            auto rootData =
                as_span(*mRootSector).subspan(serialized_reference_size);

            // if no sector is referenced we shrink our tree
            bool noSectorReferenced = std::all_of(rootData.begin(), rootData.end(),
                                                  [](std::byte v) { return v == std::byte{}; });
            if (noSectorReferenced)
            {
                VEFS_TRY(decrease_tree_depth(mRootInfo.tree_depth - 1));
                VEFS_TRY(sync_to_device(mRootSector));
            }
        }

        using invoke_result_type = std::invoke_result_t<CommitFn, root_sector_info>;
        if constexpr (std::is_void_v<invoke_result_type>)
        {
            std::invoke(std::forward<CommitFn>(commitFn), (mRootInfo));
        }
        else
        {
            VEFS_TRY(std::invoke(std::forward<CommitFn>(commitFn), (mRootInfo)));
        }

        VEFS_TRY(mTreeAllocator.on_commit());

        return success();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    template <bool ReturnParentIfNotAllocated>
    inline auto sector_tree_mt<TreeAllocator, Executor, MutexType>::access(
        tree_path::const_iterator pathBegin, tree_path::const_iterator pathEnd)
        -> result<sector_handle>
    {
        sector_handle base;
        auto it =
            std::find_if(std::make_reverse_iterator(pathEnd),
                         std::make_reverse_iterator(pathBegin),
                         [this, &base](tree_position position) {
                             return (base = mSectorCache.try_access(position));
                         })
                .base();

        // current root is always in cache, i.e. if nothing is hit, it's out of
        // range
        if (!base)
        {
            return archive_errc::sector_reference_out_of_range;
        }

        if (it != pathEnd)
        {
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
        }
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

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline void sector_tree_mt<TreeAllocator, Executor, MutexType>::notify_dirty(
        sector_handle h) noexcept
    {
        mExecutor.execute([this, h = std::move(h)]() mutable {
            if (!h || !mCommitSync.try_lock_shared())
            {
                return;
            }
            std::shared_lock commitLock(mCommitSync, std::adopt_lock);

            std::unique_lock sectorLock{*h};
            if (!h.is_dirty())
            {
                return;
            }

            if (h->node_position().layer() > 0 &&
                h->node_position().position() != 0)
            {
                // we automagically deallocate reference nodes which don't
                // reference anything.
                // h->node_position().position() != 0 is a shortcut - the first
                // data node is always allocated, therefore any node
                // (indirectly) referencing the first data node will fail the
                // next test. These reference nodes are managed by the tree
                // height functions
                auto sector = as_span(*h);
                if (std::all_of(sector.begin(), sector.end(),
                                [](std::byte b) { return b == std::byte{}; }))
                {
                    // empty reference sector
                    auto parent = h->parent();
                    auto position = h->node_position();
                    auto childOffset = position.parent_array_offset();
                    h.mark_clean();

                    sectorLock.unlock();
                    h = nullptr;

                    // #TODO reference node erasure is susceptible to toctou
                    (void)try_erase_child(std::move(parent), position,
                                          childOffset);
                    return;
                }
            }

            // sync error is reported via sector policy
            if (auto syncrx = sync_to_device(h))
            {
            }
        });
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto sector_tree_mt<TreeAllocator, Executor, MutexType>::sync_to_device(
        sector_handle h) noexcept -> result<void>
    {
        if (!h.is_dirty())
        {
            return success();
        }

        VEFS_TRY(auto &&writePosition, mTreeAllocator.reallocate(h->mNodeAllocator));

        sector_reference updated{writePosition, {}};
        if (result<void> writerx = mDevice.write_sector(
                updated.mac, mCryptoCtx, updated.sector, as_span(*h));
            writerx.has_failure())
        {
            // #TODO properly report failure with next commit
            // policy.sync_failed(writerx, writePosition);
            return std::move(writerx).as_failure();
        }

        if (auto &&parent = h->parent())
        {
            sector &parentSector = *parent;
            const auto offset = h->node_position().parent_array_offset();
            reference_sector_layout parentLayout{as_span(parentSector)};

            std::shared_lock parentLock{parentSector};

            parentLayout.write(offset, updated);

            parent.mark_dirty();
        }
        else
        {
            mRootInfo.root = updated;
        }
        h.mark_clean();
        return success();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto sector_tree_mt<TreeAllocator, Executor, MutexType>::adjust_tree_depth(
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

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto sector_tree_mt<TreeAllocator, Executor, MutexType>::increase_tree_depth(
        const int targetDepth) noexcept -> result<void>
    {
        using boost::container::static_vector;

        // const int depthDifference = targetDepth - mRootInfo.tree_depth;

        // static_vector<sector_id, lut::max_tree_depth + 1> allocatedSectors;
        // utils::scope_guard allocationRollbackGuard = [this,
        //                                              &allocatedSectors]() {
        //    for (auto it = allocatedSectors.begin(),
        //              end = allocatedSectors.end();
        //         it != end; ++it)
        //    {
        //        (void)mTreeAllocator.dealloc_one(*it);
        //    }
        //};

        //// we allocate the required disc space before making any changes,
        //// because it is the only thing that can fail
        // allocatedSectors.resize(depthDifference);
        // VEFS_TRY(mTreeAllocator.alloc_multiple(allocatedSectors));

        // we grow bottom to top in order to not disturb any ongoing access
        for (int i = mRootInfo.tree_depth; i < targetDepth; ++i)
        {
            // auto sectorId = allocatedSectors.back();
            // allocatedSectors.pop_back();
            tree_position nextRootPos(0u, i + 1);

            std::unique_lock oldRootLock{*mRootSector};
            auto createrx = mSectorCache.access(
                nextRootPos,
                [ this, nextRootPos ](
                    void *mem) noexcept->result<sector_type *, void> {
                    auto xsec = new (mem)
                        sector_type(*this, nullptr, nextRootPos, sector_id{});
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
            root.mark_dirty();

            mRootSector->parent(root);
            mRootInfo.root = {};
            oldRootLock.unlock();
            mRootSector = std::move(root);
        }
        mRootInfo.tree_depth = targetDepth;
        return success();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto sector_tree_mt<TreeAllocator, Executor, MutexType>::decrease_tree_depth(
        int targetDepth) noexcept -> result<void>
    {
        using boost::container::static_vector;

        const tree_path accessPath(tree_position(0, targetDepth));
        VEFS_TRY(auto &&newRoot, access<false>(accessPath.begin(), accessPath.end()));

        static_vector<sector_handle, lut::max_tree_depth + 1> victimChildren;

        for (sector_handle child = newRoot, parent = newRoot->parent(); parent;
             parent = child->parent())
        {
            victimChildren.emplace_back(std::move(child));
            child = std::move(parent);
        }
        mRootSector = nullptr;

        std::for_each(
            victimChildren.rbegin(), victimChildren.rend(),
            [this](sector_handle &current) {
                std::unique_lock xguard{*current};
                auto parent = current->parent();
                current->parent(nullptr);
                auto sectorId = mRootInfo.root.sector;
                for (;;)
                {
                    mRootInfo.root =
                        reference_sector_layout{as_span(*parent)}.read(0);
                    mSectorCache.try_purge(parent);
                    xguard.unlock();
                    if (!parent)
                    {
                        break;
                    }
                    std::this_thread::yield();
                    xguard.lock();
                }
                (void)mTreeAllocator.dealloc_one(sectorId);
                current = nullptr;
            });

        mRootSector = std::move(newRoot);
        mRootInfo.tree_depth = targetDepth;
        return success();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto sector_tree_mt<TreeAllocator, Executor, MutexType>::access_or_read_child(
        sector_handle parent, tree_position childPosition,
        int childParentOffset) noexcept -> result<sector_handle>
    {
        return mSectorCache.access(
            childPosition, [&](void *mem) noexcept->result<sector_type *> {
                const auto ref = reference_sector_layout{as_span(*parent)}.read(
                    childParentOffset);

                if (ref.sector == sector_id::master)
                {
                    return archive_errc::sector_reference_out_of_range;
                }

                auto xsec = new (mem) sector_type(*this, std::move(parent),
                                                  childPosition, ref.sector);

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

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto sector_tree_mt<TreeAllocator, Executor, MutexType>::access_or_create_child(
        sector_handle parent, tree_position childPosition,
        int childParentOffset) noexcept -> result<sector_handle>
    {
        return mSectorCache.access(
            childPosition, [&](void *mem) noexcept -> result<sector_type *> {
                auto ref = reference_sector_layout{as_span(*parent)}.read(
                    childParentOffset);

                auto xsec = new (mem) sector_type(*this, std::move(parent),
                                                  childPosition, ref.sector);

                if (ref.sector != sector_id::master)
                {
                    if (auto readResult = mDevice.read_sector(
                            as_span(*xsec), mCryptoCtx, ref.sector, ref.mac);
                        readResult.has_failure())
                    {
                        std::destroy_at(xsec);
                        readResult.assume_error() << ed::sector_idx{ref.sector};
                        return std::move(readResult).as_failure();
                    }
                }
                else
                {
                    auto const content = as_span(*xsec);
                    std::memset(content.data(), 0, content.size());
                }
                return xsec;
            });
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto sector_tree_mt<TreeAllocator, Executor, MutexType>::try_erase_child(
        const sector_handle &parent, tree_position child,
        int childParentOffset) noexcept -> result<bool>
    {
        sector_id childSectorId;
        if (mSectorCache.try_purge(child, [&]() {
                reference_sector_layout parentLayout{as_span(*parent)};
                childSectorId = parentLayout.read(childParentOffset).sector;
                parentLayout.write(childParentOffset, {sector_id::master, {}});
            }))
        {
            if (childSectorId != sector_id::master)
            {
                (void)mTreeAllocator.dealloc_one(childSectorId);
                VEFS_TRY(mDevice.erase_sector(childSectorId));
            }
            return true;
        }
        return false;
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto sector_tree_mt<TreeAllocator, Executor, MutexType>::erase_child(
        sector_handle parent, tree_position child,
        int childParentOffset) noexcept -> result<void>
    {
        for (;;)
        {
            VEFS_TRY(auto &&erased, try_erase_child(parent, child, childParentOffset));
            if (erased)
            {
                return success();
            }

            std::this_thread::yield();
            if (auto h = mSectorCache.try_access(child))
            {
                h.mark_clean();
            }
        }
    }

#pragma endregion

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto read(sector_tree_mt<TreeAllocator, Executor, MutexType> &tree,
                     rw_dynblob buffer, std::uint64_t readPos) -> result<void>
    {
        auto offset = readPos % detail::sector_device::sector_payload_size;
        tree_position it{detail::lut::sector_position_of(readPos)};

        while (buffer)
        {
            VEFS_TRY(auto &&sector, tree.access(std::exchange(
                                 it, tree_position{it.position() + 1})));

            auto chunk = as_span(sector).subspan(std::exchange(offset, 0));

            auto chunked = std::min(chunk.size(), buffer.size());
            copy(chunk, std::exchange(buffer, buffer.subspan(chunked)));
        }
        return success();
    }

    template <typename TreeAllocator, typename Executor, typename MutexType>
    inline auto write(sector_tree_mt<TreeAllocator, Executor, MutexType> &tree,
                      ro_dynblob data, std::uint64_t writePos) -> result<void>
    {
        if (!data)
        {
            return outcome::success();
        }

        using write_handle = typename sector_tree_mt<TreeAllocator, Executor,
                                                     MutexType>::write_handle;

        tree_position it{lut::sector_position_of(writePos)};
        auto offset = writePos % sector_device::sector_payload_size;

        while (data)
        {
            VEFS_TRY(auto &&sector, tree.access_or_create(std::exchange(
                                        it, tree_position{it.position() + 1})));

            write_handle writableSector{std::move(sector)};

            auto buffer =
                as_span(writableSector).subspan(std::exchange(offset, 0));
            auto chunked = std::min(data.size(), buffer.size());
            copy(std::exchange(data, data.subspan(chunked)), buffer);
        }

        return success();
    }

} // namespace vefs::detail
