#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <ranges>
#include <shared_mutex>
#include <thread>

#include <boost/container/static_vector.hpp>

#include <dplx/cncr/misc.hpp>

#include <vefs/cache/cache_mt.hpp>
#include <vefs/cache/lru_policy.hpp>
#include <vefs/llfio.hpp>
#include <vefs/platform/platform.hpp>

#include "file_crypto_ctx.hpp"
#include "reference_sector_layout.hpp"
#include "root_sector_info.hpp"
#include "tree_walker.hpp"

namespace vefs::detail
{

template <typename TreeAllocator>
class sector_mt
{
    using tree_allocator = TreeAllocator;
    using node_allocation = typename TreeAllocator::sector_allocator;

public:
    using handle = cache_ng::cache_handle<tree_position, sector_mt const>;
    using writable_handle = cache_ng::cache_handle<tree_position, sector_mt>;
    using content_span = ro_blob<sector_device::sector_payload_size>;
    using writable_content_span = rw_blob<sector_device::sector_payload_size>;

private:
    handle mParent;
    node_allocation mNodeAllocation;

    mutable std::shared_mutex mSectorSync;

    std::array<std::byte, sector_device::sector_payload_size> mContent;

public:
    sector_mt(handle parent,
              tree_allocator &treeAllocator,
              sector_id current) noexcept
        : mParent(std::move(parent))
        , mNodeAllocation(treeAllocator, current)
        , mSectorSync()
    {
    }

    /**
     * retrieves a handle to the parent
     * the handle will be empty if this is the root sector
     */
    auto parent() const noexcept -> handle const &
    {
        return mParent;
    }
    /**
     * updates the parent sector reference
     */
    void parent(handle newParent) noexcept
    {
        mParent = std::move(newParent);
    }

    void lock() const noexcept
    {
        mSectorSync.lock();
    }
    auto try_lock() const noexcept -> bool
    {
        return mSectorSync.try_lock();
    }
    void unlock() const noexcept
    {
        mSectorSync.unlock();
    }
    void lock_shared() const noexcept
    {
        mSectorSync.lock_shared();
    }
    auto try_lock_shared() const noexcept -> bool
    {
        return mSectorSync.try_lock_shared();
    }
    void unlock_shared() const noexcept
    {
        mSectorSync.unlock_shared();
    }

    auto num_referenced() const noexcept -> int
    {
        constexpr int limit = reference_sector_layout::references_per_sector;
        int counter = 0;
        for (int i = 0; i < limit; ++i)
        {
            if (reference_sector_layout::read(mContent, i).sector
                != sector_id{})
            {
                counter += 1;
            }
        }
        return counter;
    }

    auto content() noexcept -> writable_content_span
    {
        return mContent;
    }
    auto content() const noexcept -> content_span
    {
        return mContent;
    }

    auto allocation() noexcept -> node_allocation &
    {
        return mNodeAllocation;
    }
    auto allocation() const noexcept -> node_allocation const &
    {
        return mNodeAllocation;
    }
};

template <typename TreeAllocator>
class sector_cache_traits
{
    sector_device &mDevice;
    file_crypto_ctx &mCryptoCtx;
    root_sector_info &mRootInfo;
    TreeAllocator &mTreeAllocator;
    std::mutex &mRootSync;

public:
    struct initializer_type
    {
        sector_device &device;
        file_crypto_ctx &cryptoCtx;
        root_sector_info &rootInfo;
        TreeAllocator &treeAllocator;
        std::mutex &rootSync;
    };
    explicit sector_cache_traits(initializer_type const &init)
        : mDevice(init.device)
        , mCryptoCtx(init.cryptoCtx)
        , mRootInfo(init.rootInfo)
        , mTreeAllocator(init.treeAllocator)
        , mRootSync(init.rootSync)
    {
    }

private:
    using handle = typename sector_mt<TreeAllocator>::handle;
    using writable_handle = typename sector_mt<TreeAllocator>::writable_handle;
    using tree_allocator = TreeAllocator;

public:
    using key_type = tree_position;
    using value_type = sector_mt<TreeAllocator>;

    using allocator_type = std::allocator<void>;
    using eviction = cache_ng::least_recently_used_policy<key_type,
                                                          unsigned short,
                                                          allocator_type>;

    struct load_context
    {
        mutable handle parent;
        int refOffset;
        bool create;
    };
    auto load(load_context const &ctx,
              [[maybe_unused]] key_type const nodePosition,
              utils::object_storage<value_type> &storage) noexcept
            -> result<std::pair<value_type *, bool>>
    {
        if (!ctx.parent)
        {
            return load_root(storage, ctx.create);
        }

        auto const ref = reference_sector_layout::read(ctx.parent->content(),
                                                       ctx.refOffset);

        if (ref.sector == sector_id::master && !ctx.create)
        {
            return archive_errc::sector_reference_out_of_range;
        }

        auto *page = &storage.construct(std::move(ctx.parent), mTreeAllocator,
                                        ref.sector);

        if (ref.sector == sector_id::master)
        {
            ::vefs::fill_blob(page->content(), std::byte{});
        }
        else
        {
            auto readResult = mDevice.read_sector(page->content(), mCryptoCtx,
                                                  ref.sector, ref.mac);
            if (readResult.has_failure())
            {
                ctx.parent = std::move(page->parent());
                storage.destroy();

                readResult.assume_error() << ed::sector_idx{ref.sector};
                return std::move(readResult).as_failure();
            }
        }
        return std::pair{page, ref.sector == sector_id::master};
    }

private:
    auto load_root(utils::object_storage<value_type> &storage,
                   bool create) noexcept
            -> result<std::pair<value_type *, bool>>
    {
        auto *rootPage = &storage.construct(nullptr, mTreeAllocator,
                                            create ? sector_id{}
                                                   : mRootInfo.root.sector);
        if (create)
        {
            ::vefs::fill_blob(rootPage->content(), std::byte{});
            reference_sector_layout::write(rootPage->content(), 0,
                                           mRootInfo.root);
        }
        else
        {
            VEFS_TRY(mDevice.read_sector(rootPage->content(), mCryptoCtx,
                                         mRootInfo.root.sector,
                                         mRootInfo.root.mac));
        }

        return std::pair{rootPage, create};
    }

public:
    auto sync(key_type const nodePosition, value_type &node) noexcept
            -> result<void>
    {
        auto const referenceOffset = nodePosition.parent_array_offset();

        std::lock_guard sectorLock{node};

        auto const parent = node.parent();
        auto const content = node.content();
        if ((nodePosition.position() == 0U && nodePosition.layer() > 0
             && node.num_referenced() <= 1)
            || (nodePosition.position() != 0U && nodePosition.layer() > 0
                && node.num_referenced() == 0))
        {
            if (parent == nullptr)
            {
                std::lock_guard rootLock{mRootSync};
                mRootInfo.root = {};
            }
            else
            {
                writable_handle writableParent = parent.as_writable();
                auto const parentContent = writableParent->content();

                std::shared_lock parentLock{*writableParent};
                reference_sector_layout::write(parentContent, referenceOffset,
                                               {sector_id{}, {}});
            }

            mTreeAllocator.dealloc(node.allocation(),
                                   tree_allocator::leak_on_failure);
            return oc::success();
        }

        sector_reference updated{};
        VEFS_TRY(updated.sector, mTreeAllocator.reallocate(node.allocation()));

        VEFS_TRY_INJECT(mDevice.write_sector(updated.mac, mCryptoCtx,
                                             updated.sector, node.content()),
                        ed::sector_tree_position{nodePosition});

        if (parent == nullptr)
        {
            std::lock_guard rootLock{mRootSync};
            mRootInfo.root = updated;
        }
        else
        {
            writable_handle writableParent = parent.as_writable();
            std::shared_lock parentLock{*writableParent};

            auto const parentContent = writableParent->content();
            reference_sector_layout::write(parentContent, referenceOffset,
                                           updated);
        }
        return oc::success();
    }

    struct purge_context
    {
        int refOffset;
        bool ownsLock;
    };
    auto purge(purge_context const &ctx,
               [[maybe_unused]] key_type nodePosition,
               value_type &node) noexcept -> result<void>
    {
        if (auto writableParent = node.parent().as_writable())
        {
            reference_sector_layout::write(writableParent->content(),
                                           ctx.refOffset, {});
        }
        mTreeAllocator.dealloc(node.allocation(),
                               tree_allocator::leak_on_failure);
        if (ctx.ownsLock)
        {
            node.unlock();
        }
        return oc::success();
    }
};

/**
 * Thread-safe implementation of a tree of (file-)sectors used to read and write
 * sectors of a single vfile through a cache.
 *
 * While this class represents a recursive data structure, the class itself is
 * not structured recursively. Each sector, beginning with the root sector,
 * contains a number of sector_reference objects used to locate a physical block
 * on a storage device, down to data sectors which hold actual payload data
 * instead.

 * Each sector is identified by a tree_position object which consists of a layer
 * number and a position. Data sectors are always at layer zero and can be
 * distinguished from reference sectors thusly. The position numbers sectors
 * within a layer from left to right. The maximum number of bytes that can be
 * stored in a single vfile is 2^64, and, together with the limits on
 * sector_references per reference sector, this works out to a maximum number of
 * five layers (one data sector layer and four reference sector layers). See
 * tree_lut.hpp for further details on limits on sector trees.
 */
template <typename TreeAllocator>
class sector_tree_mt
{
public:
    using tree_allocator = TreeAllocator;

private:
    using sector = sector_mt<TreeAllocator>;
    using traits = sector_cache_traits<TreeAllocator>;
    using sector_cache = cache_ng::cache_mt<traits>;
    using sector_handle = typename sector_cache::handle;

    root_sector_info mRootInfo;

    tree_allocator mTreeAllocator;
    std::mutex mRootSync;
    sector_cache mSectorCache;
    sector_handle mRootSector; // needs to be destructed before mSectorCache

private:
    using sector_type = sector;

    template <typename... AllocatorCtorArgs>
    sector_tree_mt(sector_device &device,
                   file_crypto_ctx &cryptoCtx,
                   root_sector_info rootInfo,
                   AllocatorCtorArgs &&...allocatorCtorArgs)
        : mRootInfo(rootInfo)
        , mTreeAllocator(std::forward<AllocatorCtorArgs>(allocatorCtorArgs)...)
        , mRootSync()
        , mSectorCache(1024U,
                       {
                               .device = device,
                               .cryptoCtx = cryptoCtx,
                               .rootInfo = mRootInfo,
                               .treeAllocator = mTreeAllocator,
                               .rootSync = mRootSync,
                       })
        , mRootSector()
    {
    }

    auto initialize(bool createNew) noexcept -> result<void>
    {
        tree_position const rootPosition(0U, mRootInfo.tree_depth);
        if (mRootInfo.tree_depth == 0)
        {
            typename traits::load_context rootLoadCtx{
                    .parent = {},
                    .refOffset = 0,
                    .create = true,
            };
            VEFS_TRY(mRootSector, mSectorCache.pin_or_load(
                                          rootLoadCtx, tree_position{0U, 1}));
            if (!createNew)
            {
                auto const writableRoot = mRootSector.as_writable();
                reference_sector_layout::write(writableRoot->content(), 0,
                                               mRootInfo.root);
            }
            rootLoadCtx = typename traits::load_context{
                    .parent = mRootSector,
                    .refOffset = 0,
                    .create = createNew,
            };
            VEFS_TRY(mSectorCache.pin_or_load(rootLoadCtx, rootPosition));
        }
        else
        {
            typename traits::load_context rootLoadCtx{
                    .parent = {},
                    .refOffset = 0,
                    .create = false,
            };
            VEFS_TRY(mRootSector,
                     mSectorCache.pin_or_load(rootLoadCtx, rootPosition));

            if (mRootInfo.tree_depth > 1)
            {
                tree_path const anchorPath{
                        rootPosition.layer(), tree_position{0U, 1}
                };
                VEFS_TRY(mRootSector,
                         access<false>(anchorPath.begin(), anchorPath.end()));
            }
        }

        return oc::success();
    }

public:
    template <typename... AllocatorCtorArgs>
    static auto open_existing(sector_device &device,
                              file_crypto_ctx &cryptoCtx,
                              root_sector_info rootInfo,
                              AllocatorCtorArgs &&...args)
            -> result<std::unique_ptr<sector_tree_mt>>
    {
        std::unique_ptr<sector_tree_mt> tree;
        try
        {
            tree.reset(new sector_tree_mt(
                    device, cryptoCtx, rootInfo,
                    std::forward<AllocatorCtorArgs>(args)...));
        }
        catch (std::bad_alloc const &)
        {
            return errc::not_enough_memory;
        }

        VEFS_TRY(tree->initialize(false));
        return oc::success(std::move(tree));
    }

    template <typename... AllocatorCtorArgs>
    static auto create_new(sector_device &device,
                           file_crypto_ctx &cryptoCtx,
                           AllocatorCtorArgs &&...args)
            -> result<std::unique_ptr<sector_tree_mt>>
    {
        std::unique_ptr<sector_tree_mt> tree;
        try
        {
            tree.reset(new sector_tree_mt(
                    device, cryptoCtx, {},
                    std::forward<AllocatorCtorArgs>(args)...));
        }
        catch (std::bad_alloc const &)
        {
            return errc::not_enough_memory;
        }

        VEFS_TRY(tree->initialize(true));
        return oc::success(std::move(tree));
    }

    class write_handle final : cache_ng::cache_handle<tree_position, sector>
    {
        using base_type = cache_ng::cache_handle<tree_position, sector>;

    public:
        write_handle() noexcept = default;

        explicit write_handle(base_type &&base) noexcept
            : base_type(std::move(base))
        {
        }

        using base_type::base_type;
        using base_type::operator bool;
        using base_type::operator*;
        using base_type::operator->;
        using base_type::get;

        auto node_position() const noexcept -> tree_position
        {
            return base_type::key();
        }

        friend inline auto as_span(write_handle const &self) noexcept
                -> rw_blob<sector_device::sector_payload_size>
        {
            return self->content();
        }
    };

    class read_handle final
        : cache_ng::cache_handle<tree_position, sector const>
    {
        using base_type = cache_ng::cache_handle<tree_position, sector const>;

    public:
        read_handle() noexcept = default;

        explicit read_handle(base_type &&base) noexcept
            : base_type(std::move(base))
        {
        }

        using base_type::base_type;
        using base_type::operator bool;
        using base_type::operator*;
        using base_type::operator->;
        using base_type::get;

        auto as_writable() &&noexcept -> write_handle
        {
            return write_handle(static_cast<base_type &&>(*this).as_writable());
        }
        auto as_writable() const &noexcept -> write_handle
        {
            return write_handle(base_type::as_writable());
        }

        auto node_position() const noexcept -> tree_position
        {
            return base_type::key();
        }

        friend inline auto as_span(read_handle const &self) noexcept
                -> ro_blob<sector_device::sector_payload_size>
        {
            return self->content();
        }
    };

    /**
     * Tries to access from or load into cache the sector at the given node
     * position. Fails if the sector is not allocated.
     */
    auto access(tree_position nodePosition) -> result<read_handle>
    {
        tree_path const accessPath(nodePosition);
        VEFS_TRY(auto &&node,
                 access<false>(accessPath.begin(), accessPath.end()));
        return read_handle(std::move(node));
    }
    /**
     * Tries to access the sector at the given node position and creates
     * said sector if it doesn't exist.
     */
    auto access_or_create(tree_position node) -> result<read_handle>
    {
        using boost::container::static_vector;

        tree_path const sectorPath{node};
        VEFS_TRY(sector_handle mountPoint,
                 access<true>(sectorPath.begin(), sectorPath.end()));
        if (mountPoint.key() == node)
        {
            return read_handle(std::move(mountPoint));
        }

        for (auto it
             = tree_path::iterator(sectorPath, mountPoint.key().layer() - 1),
             end = sectorPath.end();
             it != end; ++it)
        {
            typename traits::load_context childLoadContext{
                    .parent = std::move(mountPoint),
                    .refOffset = it.array_offset(),
                    .create = true,
            };
            VEFS_TRY(mountPoint,
                     mSectorCache.pin_or_load(childLoadContext, *it));
        }
        return read_handle(std::move(mountPoint));
    }
    /**
     * Erase a leaf node at the given position.
     */
    auto erase_leaf(std::uint64_t leafId) -> result<void>
    {
        if (leafId == 0U)
        {
            return errc::not_supported;
        }

        tree_position const leafPos(leafId, 0);
        tree_path const leafPath(5, leafPos);

        sector_handle leaf;
        if (auto accessrx = access<false>(leafPath.cbegin(), leafPath.cend());
            accessrx.has_value())
        {
            leaf = std::move(accessrx).assume_value();
        }
        else if (accessrx.assume_error()
                 == archive_errc::sector_reference_out_of_range)
        {
            // leaf not allocated
            return success();
        }
        else
        {
            return std::move(accessrx).as_failure();
        }

        typename traits::purge_context purgeContext{
                .refOffset = leafPath.offset(0),
                .ownsLock = false,
        };
        return mSectorCache.purge(purgeContext, std::move(leaf));
    }

    struct anchor_commit_lock
    {
        sector_handle handle;
        std::unique_lock<sector const> lock;

        explicit anchor_commit_lock(sector_handle h)
            : handle(std::move(h))
            , lock(*handle, std::adopt_lock)
        {
        }
    };

    /**
     * Forces all cached information to be written to disc.
     */
    template <typename CommitFn>
    auto commit(CommitFn &&commitFn) -> result<void>
    {
        using boost::container::static_vector;

        bool anyDirty = true;
        for (int i = 0; anyDirty && i <= lut::max_tree_depth; ++i)
        {
            VEFS_TRY(anyDirty, mSectorCache.sync_all());
        }

        static_vector<anchor_commit_lock, lut::max_tree_depth> anchors;
        for (sector_handle it = mRootSector; it; it = it->parent())
        {
            std::unique_lock anchorLock{*it};
            // TODO: this can (and should) be done without a retry loop
            while (it.is_dirty())
            {
                anchorLock.unlock();
                VEFS_TRY(mSectorCache.sync(it));
                anchorLock.lock();
            }
            anchors.emplace_back(it);
            anchorLock.release();
        }
        sector_handle actualRoot = nullptr;
        for (anchor_commit_lock &it : std::ranges::reverse_view(anchors))
        {
            if (it.handle->num_referenced() > 1)
            {
                actualRoot = it.handle;
                break;
            }
        }
        if (actualRoot == nullptr)
        {
            mRootInfo.root
                    = reference_sector_layout::read(mRootSector->content(), 0);
            mRootInfo.tree_depth = 0;
        }
        else
        {
            if (auto const parent = actualRoot->parent())
            {
                mRootInfo.root
                        = reference_sector_layout::read(parent->content(), 0);
            }
            mRootInfo.tree_depth = actualRoot.key().layer();
        }

        // try to shrink the tree height to fit
        for (size_t i = anchors.size() - 1U;
             i != 0U && anchors[i].handle.key().layer() > mRootInfo.tree_depth;
             --i)
        {
            typename traits::purge_context purgeContext{
                    .refOffset = 0,
                    .ownsLock = true,
            };
            anchors[i - 1U].handle.as_writable()->parent(sector_handle{});
            if (auto purgeRx = mSectorCache.purge(purgeContext,
                                                  std::move(anchors[i].handle));
                purgeRx.has_value())
            {
                anchors[i].lock.release();
            }
            else
            {
                anchors[i - 1U].handle.as_writable()->parent(anchors[i].handle);
                anchors[i - 1U].handle.mark_clean();
                break;
            }
        }

        using invoke_result_type
                = std::invoke_result_t<CommitFn, root_sector_info>;
        if constexpr (std::is_void_v<invoke_result_type>)
        {
            std::invoke(std::forward<CommitFn>(commitFn), (mRootInfo));
        }
        else
        {
            VEFS_TRY(
                    std::invoke(std::forward<CommitFn>(commitFn), (mRootInfo)));
        }

        VEFS_TRY(mTreeAllocator.on_commit());

        return success();
    }

private:
    template <bool ReturnParentIfNotAllocated>
    auto access(tree_path::const_iterator const pathBegin,
                tree_path::const_iterator const pathEnd)
            -> result<sector_handle>
    {
        sector_handle base;
        auto it = std::find_if(
                          std::make_reverse_iterator(pathEnd),
                          std::make_reverse_iterator(pathBegin),
                          [this, &base](tree_position position)
                          { return (base = mSectorCache.try_pin(position)); })
                          .base();

        // current root is always in cache, i.e. if nothing is hit, it's out of
        // range
        if (!base)
        {
            if constexpr (ReturnParentIfNotAllocated)
            {
                return get_anchor_sector(pathBegin->layer());
            }
            else
            {
                return archive_errc::sector_reference_out_of_range;
            }
        }

        if (it != pathEnd)
        {
            // next sector is unlikely to be in the page cache,
            // therefore it is even more unlikely that its reference
            // resides in the CPU cache
            // however this only holds for the first reference load,
            // because afterwards the freshly decrypted sector content
            // will still reside in cache
            VEFS_PREFETCH_NTA(base->content().data()
                              + it.array_offset()
                                        * reference_sector_layout::
                                                serialized_reference_size);
        }
        for (; it != pathEnd; ++it)
        {
            typename traits::load_context childLoadContext{
                    .parent = std::move(base),
                    .refOffset = it.array_offset(),
                    .create = false,
            };
            if (auto entry = mSectorCache.pin_or_load(childLoadContext, *it);
                entry.has_value())
            {
                base = std::move(entry).assume_value();
            }
            else
            {
                if constexpr (ReturnParentIfNotAllocated)
                {
                    if (entry.assume_error()
                        == archive_errc::sector_reference_out_of_range)
                    {
                        return std::move(childLoadContext.parent);
                    }
                }
                return std::move(entry).as_failure();
            }
        }
        return base;
    }
    auto get_anchor_sector(int anchorDepth) -> result<sector_handle>
    {
        sector_handle anchor = mSectorCache.try_pin(tree_position{0U, 1});
        sector_handle parent;
        for (int i = 1; i < anchorDepth; ++i, anchor = std::move(parent))
        {
            tree_position nextRootPos(0u, i + 1);

            {
                std::shared_lock sharedAnchorLock{*anchor};

                parent = anchor->parent();
                if (parent != nullptr)
                {
                    continue;
                }
            }

            std::unique_lock uniqueAnchorLock{*anchor};

            // no TOCTOU for you!
            // since we cannot upgrade our shared lock, we need to recheck
            // in order to ensure correctness while racing
            parent = anchor->parent();
            if (parent != nullptr)
            {
                continue;
            }

            typename traits::load_context rootLoadContext{
                    .parent = {},
                    .refOffset = 0,
                    .create = true,
            };
            VEFS_TRY(parent,
                     mSectorCache.pin_or_load(rootLoadContext, nextRootPos));

            auto const writableAnchor = anchor.as_writable();
            writableAnchor->parent(parent);
        }
        return anchor;
    }

    static auto countReferenced(sector *page) noexcept -> int
    {
        auto const content = page->content();
        int numReferenced = 0;
        for (int i = 0; i < static_cast<int>(
                                reference_sector_layout::references_per_sector);
             ++i)
        {
            if (reference_sector_layout::read(content, i).sector != sector_id{})
            {
                numReferenced += 1;
            }
        }
        return numReferenced;
    }
};

template <typename TreeAllocator>
inline auto read(sector_tree_mt<TreeAllocator> &tree,
                 rw_dynblob buffer,
                 std::uint64_t readPos) -> result<void>
{
    auto offset = readPos % detail::sector_device::sector_payload_size;
    tree_position it{detail::lut::sector_position_of(readPos)};

    while (!buffer.empty())
    {
        VEFS_TRY(auto &&sector, tree.access(std::exchange(
                                        it, tree_position{it.position() + 1})));

        auto chunk = sector->content().subspan(std::exchange(offset, 0));

        auto chunked = std::min(chunk.size(), buffer.size());
        ::vefs::copy(chunk, std::exchange(buffer, buffer.subspan(chunked)));
    }
    return oc::success();
}

template <typename TreeAllocator>
inline auto write(sector_tree_mt<TreeAllocator> &tree,
                  ro_dynblob data,
                  std::uint64_t writePos) -> result<void>
{
    if (data.empty())
    {
        return outcome::success();
    }

    tree_position it{lut::sector_position_of(writePos)};
    auto offset = writePos % sector_device::sector_payload_size;

    // write to sectors until all data has been written
    while (!data.empty())
    {
        VEFS_TRY(auto &&sector, tree.access_or_create(std::exchange(
                                        it, tree_position{it.position() + 1})));

        auto const writableSector = std::move(sector).as_writable();

        auto const buffer
                = writableSector->content().subspan(std::exchange(offset, 0));
        auto const chunked = std::min(data.size(), buffer.size());
        ::vefs::copy(std::exchange(data, data.subspan(chunked)), buffer);
    }

    return oc::success();
}

template <typename TreeAllocator>
inline auto extract(sector_tree_mt<TreeAllocator> &tree,
                    llfio::file_handle &fileHandle,
                    std::uint64_t startPos,
                    std::uint64_t endPos) -> result<void>
{
    auto offset = startPos % detail::sector_device::sector_payload_size;
    tree_position it{detail::lut::sector_position_of(startPos)};

    while (startPos < endPos)
    {
        VEFS_TRY(auto sector, tree.access(std::exchange(
                                      it, tree_position{it.position() + 1})));

        auto chunk = as_span(sector).subspan(std::exchange(offset, 0));
        auto chunkSize = std::min(chunk.size(), endPos - startPos);

        llfio::file_handle::const_buffer_type buffers[1] = {
                {chunk.data(), chunkSize}
        };

        VEFS_TRY(fileHandle.write({buffers, startPos}));

        startPos += chunkSize;
    }

    return oc::success();
}

} // namespace vefs::detail
