#pragma once

#include <cassert>

#include <optional>

#include <boost/container/static_vector.hpp>

#include <vefs/utils/bitset_overlay.hpp>
#include <vefs/utils/misc.hpp>
#include <vefs/utils/object_storage.hpp>

#include "file_crypto_ctx.hpp"
#include "reference_sector_layout.hpp"
#include "root_sector_info.hpp"
#include "sector_device.hpp"
#include "tree_lut.hpp"
#include "tree_walker.hpp"

namespace vefs::detail
{
    template <typename TreeAllocator>
    class sector_tree_seq final
    {
    public:
        using tree_allocator_type = TreeAllocator;
        using node_allocator_type =
            typename tree_allocator_type::sector_allocator;

        enum class access_mode
        {
            read,   ///< error if node does not exist or on corruption
            create, ///< create node if not existant and fail on corruption
            force,  ///< create node if not existant and overwrite corrupted
                    ///< nodes
        };

    private:
        struct node_info
        {
            node_info(node_allocator_type sectorAllocator, bool dirty) noexcept
                : mSectorAllocator(std::move(sectorAllocator))
                , mDirty(dirty)
            {
            }

#if defined _DEBUG
            ~node_info()
            {
                assert(!mDirty);
            }
#endif

            node_allocator_type mSectorAllocator;
            bool mDirty;
        };
        using node_info_container_type =
            std::array<utils::object_storage<node_info>, lut::max_tree_depth>;

        static constexpr auto data_node_size =
            sector_device::sector_payload_size;
        static constexpr auto reference_node_size =
            sector_device::sector_payload_size;

        using data_node_content_type = span<std::byte, data_node_size>;
        using reference_node_content_type =
            span<std::byte, reference_node_size>;
        using data_storage_type =
            std::array<std::byte,
                       std::max(data_node_size, reference_node_size)>;

        using data_storage_container_type =
            std::array<data_storage_type, lut::max_tree_depth>;

        template <typename... AllocatorCtorArgs>
        sector_tree_seq(sector_device &device, file_crypto_ctx &cryptoCtx,
                        root_sector_info rootInfo,
                        AllocatorCtorArgs &&... args) noexcept;
        auto init_existing() noexcept -> result<void>;
        auto create_new() noexcept -> result<void>;

    public:
        template <typename... AllocatorCtorArgs>
        static auto open_existing(sector_device &device,
                                  file_crypto_ctx &cryptoCtx,
                                  root_sector_info rootInfo,
                                  AllocatorCtorArgs &&... args) noexcept
            -> result<std::unique_ptr<sector_tree_seq>>;
        template <typename... AllocatorCtorArgs>
        static auto create_new(sector_device &device,
                               file_crypto_ctx &cryptoCtx,
                               AllocatorCtorArgs &&... args) noexcept
            -> result<std::unique_ptr<sector_tree_seq>>;

        ~sector_tree_seq() noexcept;

        auto position() const noexcept -> tree_position
        {
            return mCurrentPath.layer_position(0);
        }
        auto move_backward(const access_mode mode = access_mode::read) noexcept
            -> result<void>;
        auto move_forward(const access_mode mode = access_mode::read) noexcept
            -> result<void>;
        auto move_to(const std::uint64_t leafPosition,
                     const access_mode mode = access_mode::read) noexcept
            -> result<void>;

        auto erase_leaf(std::uint64_t leafId) noexcept -> result<void>;
        auto erase_self() noexcept -> result<void>;

        template <typename Fn>
        auto commit(Fn &&commitFn) noexcept -> result<void>;

        auto bytes() const noexcept
            -> ro_blob<sector_device::sector_payload_size>;
        auto writeable_bytes() noexcept
            -> rw_blob<sector_device::sector_payload_size>;

        auto is_loaded() const noexcept -> bool
        {
            return mRootInfo.tree_depth == mLoaded;
        }

        auto extract_alloc_map(utils::bitset_overlay allocs) -> result<void>;

    private:
        auto move_to(const tree_path loadPath, const access_mode mode) noexcept
            -> result<void>;

        auto load_next(const int parentRefOffset) noexcept -> result<void>;
        auto load(const tree_path &newPath, tree_path::const_iterator &updateIt,
                  const tree_path::const_iterator end) noexcept -> result<void>;

        auto create_next(const int parentRefOffset) noexcept -> result<void>;
        auto create(tree_path::const_iterator updateIt,
                    const tree_path::iterator end) noexcept -> result<void>;

        auto compute_update_range(const tree_path newPath,
                                  const bool forceReload) const noexcept
            -> std::pair<tree_path::const_iterator, tree_path::const_iterator>;

        auto grow_tree(const int desiredDepth) noexcept -> result<void>;
        auto require_tree_depth(const std::uint64_t leafPosition,
                                const access_mode mode) noexcept
            -> result<void>;

        auto collect_intermediate_nodes() noexcept -> result<void>;

        auto collect_next_layer(utils::bitset_overlay bitset) -> result<void>;

        auto sync_to_device(const int layer) noexcept -> result<void>;

        auto node(const int treeLayer) noexcept -> node_info &
        {
            return mNodeInfos[treeLayer].value();
        }
        auto node_data(const int treeLayer) noexcept -> data_storage_type &
        {
            return mDataBlocks[treeLayer];
        }
        auto node_data_span(const int treeLayer) noexcept
            -> rw_blob<std::tuple_size<data_storage_type>::value>
        {
            return node_data(treeLayer);
        }
        auto ref_node(const int treeLayer) noexcept -> reference_sector_layout
        {
            return reference_sector_layout(node_data_span(treeLayer));
        }
        auto last_loaded_index() noexcept -> int
        {
            return mRootInfo.tree_depth - mLoaded;
        }

        sector_device &mDevice;
        file_crypto_ctx &mCryptoCtx;

        tree_path mCurrentPath;
        root_sector_info mRootInfo;
        int mLoaded;

        node_info_container_type mNodeInfos;

        tree_allocator_type mTreeAllocator;
        data_storage_container_type mDataBlocks;
    };

    template <typename TreeAllocator>
    template <typename... AllocatorCtorArgs>
    inline sector_tree_seq<TreeAllocator>::sector_tree_seq(
        sector_device &device, file_crypto_ctx &cryptoCtx,
        root_sector_info rootInfo,
        AllocatorCtorArgs &&... allocatorCtorArgs) noexcept
        : mDevice(device)
        , mCryptoCtx(cryptoCtx)
        , mCurrentPath(tree_position(0))
        , mRootInfo(rootInfo)
        , mLoaded(-1)
        , mNodeInfos()
        , mTreeAllocator(std::forward<AllocatorCtorArgs>(allocatorCtorArgs)...)
        , mDataBlocks()
    {
    }
    template <typename TreeAllocator>
    inline sector_tree_seq<TreeAllocator>::~sector_tree_seq() noexcept
    {
        if constexpr (!std::is_trivially_destructible_v<node_info>)
        {
            for (int i = last_loaded_index(); i <= mRootInfo.tree_depth; ++i)
            {
                mNodeInfos[i].destroy();
            }
        }
    }

    template <typename TreeAllocator>
    template <typename... AllocatorCtorArgs>
    inline auto sector_tree_seq<TreeAllocator>::open_existing(
        sector_device &device, file_crypto_ctx &cryptoCtx,
        root_sector_info rootInfo, AllocatorCtorArgs &&... args) noexcept
        -> result<std::unique_ptr<sector_tree_seq>>
    {
        std::unique_ptr<sector_tree_seq> sectorTree(
            new (std::nothrow)
                sector_tree_seq(device, cryptoCtx, rootInfo,
                                std::forward<AllocatorCtorArgs>(args)...));
        if (!sectorTree)
        {
            return errc::not_enough_memory;
        }

        VEFS_TRY(sectorTree->init_existing());

        return std::move(sectorTree);
    }

    template <typename TreeAllocator>
    inline auto sector_tree_seq<TreeAllocator>::init_existing() noexcept
        -> result<void>
    {
        VEFS_TRY(mDevice.read_sector(mDataBlocks[mRootInfo.tree_depth],
                                     mCryptoCtx, mRootInfo.root.sector,
                                     mRootInfo.root.mac));

        mNodeInfos[mRootInfo.tree_depth].construct(
            node_allocator_type{mTreeAllocator, mRootInfo.root.sector}, false);
        mLoaded = 0;

        auto [updateIt, end] = compute_update_range(mCurrentPath, true);
        return load(mCurrentPath, updateIt, end);
    }

    template <typename TreeAllocator>
    template <typename... AllocatorCtorArgs>
    inline auto sector_tree_seq<TreeAllocator>::create_new(
        sector_device &device, file_crypto_ctx &cryptoCtx,
        AllocatorCtorArgs &&... args) noexcept
        -> result<std::unique_ptr<sector_tree_seq>>
    {
        std::unique_ptr<sector_tree_seq> sectorTree(
            new (std::nothrow)
                sector_tree_seq(device, cryptoCtx, {},
                                std::forward<AllocatorCtorArgs>(args)...));
        if (!sectorTree)
        {
            return errc::not_enough_memory;
        }

        VEFS_TRY(sectorTree->create_new());

        return std::move(sectorTree);
    }

    template <typename TreeAllocator>
    inline auto sector_tree_seq<TreeAllocator>::extract_alloc_map(
        utils::bitset_overlay allocs) -> result<void>
    {
        allocs.set(static_cast<std::uint64_t>(mRootInfo.root.sector));
        if (mRootInfo.tree_depth == 0)
        {
            return success();
        }

        while (mLoaded > 0)
        {
            mNodeInfos[last_loaded_index()].destroy();
            mLoaded -= 1;
        }

        return collect_next_layer(allocs);
    }

    template <typename TreeAllocator>
    inline auto sector_tree_seq<TreeAllocator>::collect_next_layer(
        utils::bitset_overlay allocs) -> result<void>
    {
        auto layer = last_loaded_index();
        reference_sector_layout layout{node_data_span(layer)};

        for (int i = 0; i < layout.references_per_sector; ++i)
        {
            auto ref = layout.read(i);
            if (ref.sector == sector_id::master)
            {
                continue;
            }

            allocs.set(static_cast<std::uint64_t>(ref.sector));

            if (layer == 1)
            {
                continue;
            }
            VEFS_TRY(load_next(i));

            VEFS_TRY(collect_next_layer(allocs));

            mNodeInfos[layer - 1].destroy();
            mLoaded -= 1;
        }

        return success();
    }

    template <typename TreeAllocator>
    inline auto sector_tree_seq<TreeAllocator>::create_new() noexcept
        -> result<void>
    {
        // mDataBlocks[0] = {};
        mNodeInfos[0].construct(
            node_allocator_type{mTreeAllocator, sector_id{}}, true);
        mLoaded = 0;

        return success();
    }

    template <typename TreeAllocator>
    template <typename Fn>
    inline auto sector_tree_seq<TreeAllocator>::commit(Fn &&commitFn) noexcept
        -> result<void>
    {
        for (int i = 0; i <= mRootInfo.tree_depth; ++i)
        {
            VEFS_TRY(sync_to_device(i));
        }

        using invoke_result_type = std::invoke_result_t<Fn, root_sector_info>;
        if constexpr (std::is_void_v<invoke_result_type>)
        {
            std::invoke(std::forward<Fn>(commitFn), (mRootInfo));
        }
        else
        {
            VEFS_TRY(std::invoke(std::forward<Fn>(commitFn), (mRootInfo)));
        }

        VEFS_TRY(mTreeAllocator.on_commit());

        return success();
    }

    template <typename TreeAllocator>
    inline auto sector_tree_seq<TreeAllocator>::load_next(
        const int parentRefOffset) noexcept -> result<void>
    {
        auto layer = last_loaded_index() - 1;
        assert(layer >= 0);
        auto ref = ref_node(layer + 1).read(parentRefOffset);
        if (ref.sector == sector_id::master)
        {
            return archive_errc::sector_reference_out_of_range;
        }
        span storage(node_data(layer));

        VEFS_TRY(mDevice.read_sector(storage, mCryptoCtx, ref.sector, ref.mac));

        mLoaded += 1;
        mNodeInfos[layer].construct(
            node_allocator_type{mTreeAllocator, ref.sector}, false);
        return success();
    }

    template <typename TreeAllocator>
    inline auto sector_tree_seq<TreeAllocator>::move_backward(
        const access_mode mode) noexcept -> result<void>
    {
        if (mCurrentPath.position(0) == 0)
        {
            return errc::no_more_data;
        }
        return move_to(mCurrentPath.previous(), mode);
    }

    template <typename TreeAllocator>
    inline auto sector_tree_seq<TreeAllocator>::move_forward(
        const access_mode mode) noexcept -> result<void>
    {
        VEFS_TRY(require_tree_depth(mCurrentPath.position(0) + 1, mode));
        return move_to(mCurrentPath.next(), mode);
    }

    template <typename TreeAllocator>
    inline auto
    sector_tree_seq<TreeAllocator>::move_to(const std::uint64_t leafPosition,
                                            const access_mode mode) noexcept
        -> result<void>
    {
        VEFS_TRY(require_tree_depth(leafPosition, mode));
        return move_to(tree_path(tree_position(leafPosition)), mode);
    }

    template <typename TreeAllocator>
    inline auto
    sector_tree_seq<TreeAllocator>::move_to(const tree_path loadPath,
                                            const access_mode mode) noexcept
        -> result<void>
    {
        auto [updateIt, end] = compute_update_range(loadPath, false);
        if (auto loadrx = load(loadPath, updateIt, end); !loadrx)
        {
            if (mode != access_mode::read &&
                loadrx.assume_error() ==
                    archive_errc::sector_reference_out_of_range)
            {
                VEFS_TRY(create(updateIt, end));
            }
            else if (mode == access_mode::force &&
                     loadrx.assume_error() == archive_errc::tag_mismatch)
            {
                if (std::next(updateIt) != end)
                {
                    // a leaf sector allocation can be recovered,
                    // this is obviously not possible with reference sectors
                    mTreeAllocator.on_leak_detected();
                }
                VEFS_TRY(create(updateIt, end));
            }
            else
            {
                return std::move(loadrx).as_failure();
            }
        }
        return success();
    }

    template <typename TreeAllocator>
    inline auto
    sector_tree_seq<TreeAllocator>::erase_leaf(std::uint64_t leafId) noexcept
        -> result<void>
    {
        if (leafId == 0)
        {
            return errc::not_supported;
        }
        if (lut::required_tree_depth(leafId) > mRootInfo.tree_depth)
        {
            return success();
        }

        int refOffset;
        if (mCurrentPath.position(0) == leafId && is_loaded())
        {
            mNodeInfos[0].destroy();
            fill_blob(node_data_span(0));
            mLoaded -= 1;

            refOffset = mCurrentPath.offset(0);
        }
        else
        {
            tree_path loadPath{tree_position(leafId)};
            auto [updateIt, end] = compute_update_range(loadPath, false);
            std::advance(end, -1);

            if (auto loadrx = load(loadPath, updateIt, end); loadrx.has_error())
            {
                if (loadrx.assume_error() ==
                    archive_errc::sector_reference_out_of_range)
                {
                    return success();
                }
                return std::move(loadrx).as_failure();
            }
            refOffset = loadPath.offset(0);
        }

        auto refNode = ref_node(1);

        const auto ref = refNode.read(refOffset);
        VEFS_TRY(mDevice.erase_sector(ref.sector));

        mTreeAllocator.dealloc_one(ref.sector,
                                   tree_allocator_type::leak_on_failure);
        node(1).mDirty = true;
        refNode.write(refOffset, {});

        return collect_intermediate_nodes();
    }

    template <typename TreeAllocator>
    inline auto sector_tree_seq<TreeAllocator>::erase_self() noexcept
        -> result<void>
    {
        if (mRootInfo.tree_depth > 0)
        {
            return errc::bad;
        }
        node(0).mDirty = false;
        if (mRootInfo.root.sector == sector_id{})
        {
            return success();
        }
        VEFS_TRY(mDevice.erase_sector(mRootInfo.root.sector));
        mTreeAllocator.dealloc_one(mRootInfo.root.sector,
                                   tree_allocator_type::leak_on_failure);
        return success();
    }

    template <typename TreeAllocator>
    inline auto sector_tree_seq<TreeAllocator>::compute_update_range(
        const tree_path newPath, const bool forceReload) const noexcept
        -> std::pair<tree_path::const_iterator, tree_path::const_iterator>
    {
        if (forceReload)
        {
            const auto subRootDistance =
                (lut::max_tree_depth + 2) - mRootInfo.tree_depth;
            return {std::next(newPath.cbegin(), subRootDistance),
                    newPath.cend()};
        }
        else
        {
            return {std::mismatch(mCurrentPath.cbegin(), mCurrentPath.cend(),
                                  newPath.cbegin())
                        .second,
                    newPath.cend()};
        }
    }

    template <typename TreeAllocator>
    inline auto sector_tree_seq<TreeAllocator>::load(
        const tree_path &newPath, tree_path::const_iterator &updateIt,
        const tree_path::const_iterator end) noexcept -> result<void>
    {
        if (updateIt == newPath.cend())
        {
            mCurrentPath = newPath;
            return success();
        }

        const auto numChanged = updateIt->layer();
        for (int i = last_loaded_index(); i <= numChanged; ++i)
        {
            VEFS_TRY(sync_to_device(i));
            mNodeInfos[i].destroy();
            mLoaded -= 1;
        }
        mCurrentPath = newPath;

        for (; updateIt != end; ++updateIt)
        {
            VEFS_TRY_INJECT(load_next(updateIt.array_offset()),
                            ed::sector_tree_position(*updateIt));
        }
        return success();
    }

    template <typename TreeAllocator>
    inline auto sector_tree_seq<TreeAllocator>::create_next(
        const int parentRefOffset) noexcept -> result<void>
    {
        auto layer = mRootInfo.tree_depth - (mLoaded + 1);
        // we return any existing sector back to the allocator
        auto ref = ref_node(layer + 1).read(parentRefOffset);

        fill_blob(node_data_span(layer));

        mLoaded += 1;
        mNodeInfos[layer].construct(
            node_allocator_type{mTreeAllocator, ref.sector}, true);
        return success();
    }

    template <typename TreeAllocator>
    inline auto sector_tree_seq<TreeAllocator>::create(
        tree_path::const_iterator updateIt,
        const tree_path::iterator end) noexcept -> result<void>
    {
        for (; updateIt != end; ++updateIt)
        {
            VEFS_TRY(create_next(updateIt.array_offset()));
        }
        return success();
    }

    template <typename TreeAllocator>
    inline auto
    sector_tree_seq<TreeAllocator>::grow_tree(const int desiredDepth) noexcept
        -> result<void>
    {
        for (int depth = mRootInfo.tree_depth + 1; depth <= desiredDepth;
             ++depth)
        {
            mNodeInfos[depth].construct(
                node_allocator_type(mTreeAllocator, sector_id{}), true);

            fill_blob(node_data_span(depth));
            ref_node(depth).write(0, std::exchange(mRootInfo.root, {}));

            mRootInfo.tree_depth = depth;
            mLoaded += 1;
        }
        return success();
    }

    template <typename TreeAllocator>
    inline auto sector_tree_seq<TreeAllocator>::require_tree_depth(
        const std::uint64_t leafPosition, const access_mode mode) noexcept
        -> result<void>
    {
        if (const auto requiredDepth = lut::required_tree_depth(leafPosition);
            requiredDepth > mRootInfo.tree_depth)
        {
            if (mode == access_mode::read)
            {
                return archive_errc::sector_reference_out_of_range;
            }
            return grow_tree(requiredDepth);
        }
        return success();
    }

    template <typename TreeAllocator>
    inline auto
    sector_tree_seq<TreeAllocator>::collect_intermediate_nodes() noexcept
        -> result<void>
    {
        int i = 1;
        for (; i < mRootInfo.tree_depth && mCurrentPath.position(i) != 0; ++i)
        {
            auto &block = node_data(i);
            if (std::any_of(block.begin(), block.end(),
                            utils::is_non_null_byte))
            {
                return success();
            }

            node(i).mDirty = false;
            mNodeInfos[i].destroy();
            mLoaded -= 1;

            auto refs = ref_node(i + 1);
            const auto nodeRefOffset = mCurrentPath.offset(i);
            const auto nodeRef = refs.read(nodeRefOffset);
            VEFS_TRY(mDevice.erase_sector(nodeRef.sector));

            mTreeAllocator.dealloc_one(nodeRef.sector,
                                       tree_allocator_type::leak_on_failure);
            refs.write(nodeRefOffset, {});
            node(i + 1).mDirty = true;
        }

        // now shrink the tree if possible
        if (i != mRootInfo.tree_depth)
        {
            return success();
        }

        VEFS_TRY(move_to(0));

        for (; i > 0; --i)
        {
            constexpr auto serialized_reference_size =
                reference_sector_layout::serialized_reference_size;

            auto &block = node_data(i);
            if (auto xbegin =
                    std::next(block.begin(), serialized_reference_size);
                std::any_of(xbegin, block.end(), utils::is_non_null_byte))
            {
                return success();
            }

            const auto newRootRef = ref_node(i).read(0);
            VEFS_TRY(mDevice.erase_sector(mRootInfo.root.sector));
            mTreeAllocator.dealloc_one(mRootInfo.root.sector,
                                       tree_allocator_type::leak_on_failure);

            mRootInfo.root = newRootRef;
            mRootInfo.tree_depth -= 1;

            fill_blob(span(block));
            node(i).mDirty = false;
            mNodeInfos[i].destroy();
            mLoaded -= 1;
        }

        return success();
    }

    template <typename TreeAllocator>
    inline auto
    sector_tree_seq<TreeAllocator>::sync_to_device(int layer) noexcept
        -> result<void>
    {
        auto &nodeInfo = node(layer);
        if (!nodeInfo.mDirty)
        {
            return success();
        }

        VEFS_TRY(writeSector,
                 mTreeAllocator.reallocate(nodeInfo.mSectorAllocator));

        sector_reference updatedRef;
        if (auto writerx = mDevice.write_sector(
                updatedRef.mac, mCryptoCtx, writeSector, node_data_span(layer));
            !writerx)
        {
            mTreeAllocator.on_leak_detected();
            return std::move(writerx).as_failure();
        }
        updatedRef.sector = writeSector;

        if (mRootInfo.tree_depth == layer)
        {
            // we synced the root sector
            mRootInfo.root = updatedRef;
        }
        else
        {
            node(layer + 1).mDirty = true;
            ref_node(layer + 1).write(mCurrentPath.offset(layer), updatedRef);
        }
        nodeInfo.mDirty = false;
        return success();
    }

    template <typename TreeAllocator>
    inline auto sector_tree_seq<TreeAllocator>::bytes() const noexcept
        -> ro_blob<sector_device::sector_payload_size>
    {
        return mDataBlocks[0];
    }

    template <typename TreeAllocator>
    inline auto sector_tree_seq<TreeAllocator>::writeable_bytes() noexcept
        -> rw_blob<sector_device::sector_payload_size>
    {
        node(0).mDirty = true;
        return mDataBlocks[0];
    }

    template <typename TreeAllocator>
    inline auto erase_contiguous(sector_tree_seq<TreeAllocator> &tree,
                                 std::uint64_t maxExtent) noexcept
        -> result<void>
    {
        if (maxExtent > sector_device::sector_payload_size)
        {
            for (auto it = lut::sector_position_of(maxExtent - 1); it > 0; --it)
            {
                VEFS_TRY(tree.erase_leaf(it));
            }
        }

        return tree.erase_self();
    }
} // namespace vefs::detail
