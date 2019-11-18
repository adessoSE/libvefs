#pragma once

#include <optional>

#include <boost/container/static_vector.hpp>

#include "file_crypto_ctx.hpp"
#include "reference_sector_layout.hpp"
#include "root_sector_info.hpp"
#include "sector_device.hpp"
#include "tree_lut.hpp"
#include "tree_walker.hpp"

namespace vefs::detail
{
    template <typename TreeAllocator>
    class sector_tree_seq
    {
    public:
        using tree_allocator_type = TreeAllocator;
        using sector_allocator_type =
            typename tree_allocator_type::sector_allocator;

    private:
        struct node_info
        {
#if defined _DEBUG
            ~node_info()
            {
                assert(!mDirty);
            }
#endif

            sector_allocator_type mSectorAllocator;
            bool mDirty;
        };
        using node_info_container_type =
            boost::container::static_vector<node_info, lut::max_tree_depth>;

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
            boost::container::static_vector<data_storage_type,
                                            lut::max_tree_depth>;

        template <typename... AllocatorCtorArgs>
        sector_tree_seq(sector_device &device, file_crypto_ctx &cryptoCtx,
                        root_sector_info rootInfo,
                        AllocatorCtorArgs &&... args);

    public:
        template <typename... AllocatorCtorArgs>
        static auto
        open_existing(sector_device &device, file_crypto_ctx &cryptoCtx,
                      root_sector_info rootInfo, AllocatorCtorArgs &&... args)
            -> result<std::unique_ptr<sector_tree_seq>>;
        template <typename... AllocatorCtorArgs>
        static auto create_new(sector_device &device,
                               file_crypto_ctx &cryptoCtx,
                               AllocatorCtorArgs &&... args)
            -> result<std::unique_ptr<sector_tree_seq>>;

        auto move_backward() noexcept -> result<void>;
        auto move_forward() noexcept -> result<void>;
        auto move_to(std::uint64_t leafPosition) noexcept -> result<void>;

        auto commit() noexcept -> result<root_sector_info>;

        auto bytes() noexcept -> ro_blob<sector_device::sector_payload_size>;
        auto writeable_bytes() noexcept
            -> rw_blob<sector_device::sector_payload_size>;

        auto is_loaded() const noexcept -> bool
        {
            return mRootInfo.tree_depth + 1 !=
                   static_cast<int>(mNodeInfos.size());
        }

    private:
        auto load_next(int parentRefOffset) noexcept -> result<void>;
        auto load(tree_path loadPath) noexcept -> result<void>;
        auto load(tree_path loadPath, bool force) noexcept -> result<void>;

        auto sync_to_device(int layer) noexcept -> result<void>;

        auto layer_to_index(int layer) const noexcept -> int
        {
            return mRootInfo.tree_depth - layer;
        }

        auto node(int treeLayer) noexcept -> node_info &
        {
            return mNodeInfos[layer_to_index(treeLayer)];
        }
        auto node_data(int treeLayer) noexcept -> data_storage_type &
        {
            return mDataBlocks[layer_to_index(treeLayer)];
        }
        auto ref_node(int treeLayer) -> reference_sector_layout
        {
            return reference_sector_layout(span(node_data(treeLayer)));
        }

        sector_device &mDevice;
        file_crypto_ctx &mCryptoCtx;

        tree_path mCurrentPath;
        root_sector_info mRootInfo;

        node_info_container_type mNodeInfos;

        tree_allocator_type mTreeAllocator;
        data_storage_container_type mDataBlocks;
    };

    template <typename TreeAllocator>
    template <typename... AllocatorCtorArgs>
    inline sector_tree_seq<TreeAllocator>::sector_tree_seq(
        sector_device &device, file_crypto_ctx &cryptoCtx,
        root_sector_info rootInfo, AllocatorCtorArgs &&... args)
        : mDevice(device)
        , mCryptoCtx(cryptoCtx)
        , mCurrentPath(tree_position(0))
        , mRootInfo(rootInfo)
        , mNodeInfos()
        , mTreeAllocator(std::forward<AllocatorCtorArgs>(allocatorCtorArgs)...)
        , mDataBlocks()
    {
    }

    template <typename TreeAllocator>
    inline auto
    sector_tree_seq<TreeAllocator>::load_next(int parentRefOffset) noexcept
        -> result<void>
    {
        auto layer = mRootInfo.tree_depth - static_cast<int>(mNodeInfos.size());
        auto ref = ref_node(layer + 1).read(parentRefOffset);
        if (ref.sector == sector_id::master)
        {
            return archive_errc::sector_reference_out_of_range;
        }
        span storage(node_data(layer));

        VEFS_TRY(mDevice.read_sector(storage, mCryptoCtx, ref.sector, ref.mac));

        mNodeInfos.push_back(node_info{
            sector_allocator_type{mTreeAllocator, ref.sector}, false});
        return success();
    }

    template <typename TreeAllocator>
    inline auto sector_tree_seq<TreeAllocator>::move_backward() noexcept
        -> result<void>
    {
        if (mCurrentPath.position(0) == 0)
        {
            return errc::no_more_data;
        }
        return load(mCurrentPath.previous());
    }

    template <typename TreeAllocator>
    inline auto sector_tree_seq<TreeAllocator>::move_forward() noexcept
        -> result<void>
    {
        return load(mCurrentPath.next());
    }

    template <typename TreeAllocator>
    inline auto
    sector_tree_seq<TreeAllocator>::load(tree_path loadPath) noexcept
        -> result<void>
    {
        return load(loadPath, !is_loaded());
    }

    template <typename TreeAllocator>
    inline auto
    sector_tree_seq<TreeAllocator>::move_to(std::uint64_t leafPosition) noexcept
        -> result<void>
    {
        return load(tree_path(tree_position(leafPosition)));
    }

    template <typename TreeAllocator>
    inline auto sector_tree_seq<TreeAllocator>::load(tree_path loadPath,
                                                     bool force) noexcept
        -> result<void>
    {
        tree_path::iterator updateIt;
        if (force)
        {
            // #TODO validate depth arithmetic
            updateIt = std::next(loadPath.begin(),
                                 lut::max_tree_depth - mRootInfo.tree_depth);
        }
        else
        {
            auto updateIt = std::mismatch(mCurrentPath.begin(),
                                          mCurrentPath.end(), loadPath.begin())
                                .second;
        }
        const auto numChanged = std::distance(updateIt, loadPath.end());
        for (int i = 0; i < numChanged; ++i)
        {
            VEFS_TRY(sync_to_device(i));
            mNodeInfos.pop_back();
        }

        for (const auto updateEnd = loadPath.end(); updateIt != updateEnd;
             ++updateIt)
        {
            VEFS_TRY(load_next(updateIt.array_offset()));
        }
        mCurrentPath = loadPath;
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

        // #TODO what happens to the allocated sector in case of failure?
        sector_reference updatedRef;
        VEFS_TRY(mDevice.write_sector(updatedRef.mac, mCryptoCtx, writeSector,
                                      node_data(layer)));
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
} // namespace vefs::detail
