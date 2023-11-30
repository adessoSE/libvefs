#include "vfilesystem.hpp"

#include <boost/container/small_vector.hpp>
#include <boost/endian/conversion.hpp>

#include <dplx/predef/compiler/gcc.h>

#include <dplx/dp/api.hpp>
#include <dplx/dp/items/emit_core.hpp>
#include <dplx/dp/items/item_size_of_ranges.hpp>
#include <dplx/dp/items/parse_core.hpp>
#include <dplx/dp/legacy/chunked_input_stream.hpp>
#include <dplx/dp/legacy/chunked_output_stream.hpp>
#include <dplx/dp/legacy/memory_buffer.hpp>
#include <dplx/dp/streams/memory_input_stream.hpp>
#include <dplx/dp/streams/memory_output_stream.hpp>

#include <vefs/utils/binary_codec.hpp>
#include <vefs/utils/bit.hpp>
#include <vefs/utils/bitset_overlay.hpp>
#include <vefs/utils/random.hpp>

#include "detail/archive_sector_allocator.hpp"
#include "detail/archive_tree_allocator.hpp"
#include "detail/file_descriptor.hpp"
#include "detail/sector_tree_seq.hpp"
#include "platform/sysrandom.hpp"

namespace vefs
{

class vfilesystem::index_tree_layout
{
public:
    static constexpr inline auto sector_payload_size
            = detail::sector_device::sector_payload_size;

    static constexpr std::uint64_t block_size = 64u;
    static constexpr std::uint64_t alloc_map_size = 64u;
    static constexpr auto blocks_per_sector
            = (sector_payload_size - alloc_map_size) / block_size;

    static_assert(alloc_map_size
                          * std::numeric_limits<
                                  std::underlying_type_t<std::byte>>::digits
                  > blocks_per_sector);

private:
    static constexpr auto block_to_tree_position(int const block)
            -> std::uint64_t
    {
        return static_cast<std::uint64_t>(block) / blocks_per_sector;
    }

    static constexpr auto block_to_file_position(int const block)
            -> std::uint64_t
    {
        auto const wblock = static_cast<std::uint64_t>(block);

        auto const treePosition = wblock / blocks_per_sector;
        auto const treeOffset = wblock % blocks_per_sector;

        return treePosition * sector_payload_size + alloc_map_size
               + treeOffset * block_size;
    }

    using map_bucket_type = std::size_t;
    static constexpr auto map_bucket_size
            = std::numeric_limits<map_bucket_type>::digits;
    static constexpr auto map_buckets_per_sector
            = alloc_map_size / sizeof(map_bucket_type);

    enum block_find_mode
    {
        occupied,
        unoccupied,
    };

    template <block_find_mode Mode>
    [[nodiscard]] static auto
    find_next(std::span<std::byte const, alloc_map_size> allocMap,
              unsigned int begin) noexcept -> unsigned int
    {
        using namespace boost::endian;

        auto offset = begin / map_bucket_size;
        auto start = begin % map_bucket_size;
        for (; offset < map_buckets_per_sector; ++offset)
        {
            map_bucket_type eblock
                    = endian_load<map_bucket_type, sizeof(map_bucket_type),
                                  order::little>(
                            reinterpret_cast<unsigned char const *>(
                                    allocMap.data()
                                    + offset * sizeof(map_bucket_type)));

            if constexpr (Mode == occupied)
            {
                eblock = eblock >> start;
            }
            else
            {
                // note the complement operation
                //       v
                eblock = ~eblock >> start;
            }
            if (eblock != 0)
            {
                return offset * map_bucket_size + start
                       + utils::countr_zero(eblock);
            }
            start = 0;
        }
        return blocks_per_sector;
    }

    struct tree_stream_position
    {
        tree_type::read_handle sector;
        int next_block;
    };

#if defined(DPLX_COMP_GNUC_AVAILABLE) && !defined(DPLX_COMP_GNUC_EMULATED)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

    class tree_input_stream final
        : public dplx::dp::legacy::chunked_input_stream_base<tree_input_stream>
    {
        friend class dplx::dp::legacy::chunked_input_stream_base<
                tree_input_stream>;
        using base_type = dplx::dp::legacy::chunked_input_stream_base<
                tree_input_stream>;

        tree_type *mTree;
        tree_type::read_handle mCurrentSector;

        struct stream_info
        {
            unsigned int prefix_size;
            std::uint32_t stream_size;
        };

        explicit tree_input_stream(
                std::span<std::byte const> const initialReadArea,
                std::uint64_t streamSize,
                tree_type *tree,
                tree_type::read_handle currentSector) noexcept
            : base_type(initialReadArea, streamSize)
            , mTree(tree)
            , mCurrentSector(std::move(currentSector))
        {
        }

    public:
        static auto open(tree_type *tree,
                         tree_type::read_handle initialSector,
                         int blockOffset) noexcept -> result<tree_input_stream>
        {
            std::span<std::byte const> sectorContent = as_span(initialSector);

            auto const nextUnoccupied = find_next<unoccupied>(
                    sectorContent.first<alloc_map_size>(), blockOffset);

            auto const numAvailableBlocks = nextUnoccupied - blockOffset;
            auto const maxChunkSize = static_cast<std::uint32_t>(
                    numAvailableBlocks * block_size);

            sectorContent = sectorContent.subspan(
                    alloc_map_size + blockOffset * block_size, maxChunkSize);

            VEFS_TRY(auto streamInfo, parse_stream_prefix(sectorContent));

            auto const initialChunkSize
                    = std::min(streamInfo.stream_size,
                               maxChunkSize - streamInfo.prefix_size);
            sectorContent = sectorContent.subspan(streamInfo.prefix_size,
                                                  initialChunkSize);

            return tree_input_stream(sectorContent, streamInfo.stream_size,
                                     tree, std::move(initialSector));
        }

        [[nodiscard]] auto next_block() const noexcept -> tree_stream_position
        {
            auto const state = current_read_area();
            auto const sectorContentBegin = as_span(mCurrentSector).data();

            auto const blockOffset = state.remaining_begin()
                                     - sectorContentBegin - alloc_map_size;
            auto const nextBlock = static_cast<int>(
                    utils::div_ceil(blockOffset, block_size));

            return {.sector = mCurrentSector, .next_block = nextBlock};
        }

    private:
        static auto parse_stream_prefix(std::span<std::byte const> content)
                -> result<stream_info>
        {
            using namespace dplx;
            // false positive--code doesn't compile otherwise
            // NOLINTNEXTLINE(performance-move-const-arg)
            auto &&buffer = dp::get_input_buffer(std::move(content));
            dp::parse_context ctx{buffer};

            DPLX_TRY(dp::item_head const &streamInfo, dp::parse_item_head(ctx));

            if (streamInfo.type != dp::type_code::binary)
            {
                return dplx::dp::errc::item_type_mismatch;
            }
            if (streamInfo.value > std::numeric_limits<std::uint32_t>::max())
            {
                return dplx::dp::errc::item_value_out_of_range;
            }

            return stream_info{.prefix_size = streamInfo.encoded_length,
                               .stream_size
                               = static_cast<std::uint32_t>(streamInfo.value)};
        }

        // pin_or_load is marked noexcept… but clang-tidy doesn't care :/
        // NOLINTNEXTLINE(bugprone-exception-escape)
        auto acquire_next_chunk_impl(std::uint64_t remaining) noexcept
                -> dplx::dp::result<dplx::dp::memory_view>
        {
            auto const currentPosition = mCurrentSector.node_position();
            auto const nextPosition = next(currentPosition);

            DPLX_TRY(mCurrentSector, mTree->access(nextPosition));

            std::span<std::byte const> const memory = as_span(mCurrentSector);

            auto const firstUnallocated
                    = find_next<unoccupied>(memory.first<alloc_map_size>(), 0);

            auto const nextChunkSize
                    = std::min(remaining, blocks_per_sector * block_size);
            if (firstUnallocated < utils::div_ceil(nextChunkSize, block_size))
            {
                return dplx::dp::errc::end_of_stream;
            }

            return dplx::dp::memory_view(
                    memory.subspan(alloc_map_size, nextChunkSize));
        }
    };

    // pin_or_load is marked noexcept… but clang-tidy doesn't care :/
    // NOLINTNEXTLINE(bugprone-exception-escape)
    auto find_next_entry(tree_stream_position begin) noexcept
            -> result<tree_stream_position>
    {
        if (begin.next_block < static_cast<int>(blocks_per_sector))
        {
            std::span<std::byte const> const sectorContent
                    = as_span(begin.sector);
            begin.next_block = find_next<occupied>(
                    sectorContent.first<alloc_map_size>(), begin.next_block);
        }

        while (begin.next_block >= static_cast<int>(blocks_per_sector))
        {
            auto const nextPosition = next(begin.sector.node_position());

            VEFS_TRY(begin.sector, mIndexTree.access(nextPosition));

            std::span<std::byte const> const sectorContent
                    = as_span(begin.sector);
            begin.next_block = find_next<occupied>(
                    sectorContent.first<alloc_map_size>(), 0u);
        }

        return begin;
    }

    class tree_writer final
        : public dplx::dp::legacy::chunked_output_stream_base<tree_writer>
    {
        friend class dplx::dp::legacy::chunked_output_stream_base<tree_writer>;
        using base_type
                = dplx::dp::legacy::chunked_output_stream_base<tree_writer>;

        index_tree_layout *mOwner;
        tree_type::write_handle mCurrentSector;

        explicit tree_writer(index_tree_layout &owner,
                             tree_type::write_handle &&currentSector,
                             std::uint64_t inSectorOffset,
                             std::uint64_t inSectorSize,
                             std::uint64_t streamSize)
            : base_type(as_span(currentSector)
                                .subspan(inSectorOffset, inSectorSize),
                        streamSize - inSectorSize)
            , mOwner(&owner)
            , mCurrentSector(std::move(currentSector))
        {
        }

        static auto write_byte_stream_prefix(tree_type::write_handle &handle,
                                             std::uint64_t offset,
                                             std::uint32_t size)
                -> result<std::uint64_t>
        {
            dplx::dp::memory_output_stream buffer(
                    as_span(handle).subspan(offset, block_size));
            dplx::dp::emit_context ctx{buffer};

            VEFS_TRY(dplx::dp::emit_binary(ctx, size));

            return buffer.written_size();
        }

    public:
        static auto create(index_tree_layout &owner,
                           int firstBlock,
                           int encodedSize) -> result<tree_writer>
        {
            auto const offset = block_to_file_position(firstBlock);
            auto const size = static_cast<std::uint64_t>(encodedSize);

            auto firstPosition = detail::lut::sector_position_of(offset);
            auto inSectorOffset
                    = offset
                      - firstPosition
                                * detail::sector_device::sector_payload_size;

            VEFS_TRY(auto &&firstSector,
                     owner.mIndexTree.access(
                             detail::tree_position(firstPosition)));

            tree_type::write_handle writeHandle
                    = std::move(firstSector).as_writable();
            owner.write_block_header(writeHandle);
            VEFS_TRY(auto prefixSize,
                     write_byte_stream_prefix(writeHandle, inSectorOffset,
                                              encodedSize));

            auto const remainingSectorSize
                    = detail::sector_device::sector_payload_size
                      - (inSectorOffset + prefixSize);
            return tree_writer(
                    owner, std::move(writeHandle), inSectorOffset + prefixSize,
                    size <= remainingSectorSize ? size : remainingSectorSize,
                    size);
        }

    private:
        // pin_or_load is marked noexcept… but clang-tidy doesn't care :/
        // NOLINTNEXTLINE(bugprone-exception-escape)
        auto acquire_next_chunk_impl() noexcept
                -> dplx::dp::result<std::span<std::byte>>
        {
            auto const nextPosition = next(mCurrentSector.node_position());

            if (auto accessRx = mOwner->mIndexTree.access(nextPosition);
                accessRx.has_failure()) [[unlikely]]
            {
                // #TODO implement underlying error forwarding
                return dplx::dp::errc::bad;
            }
            else [[likely]]
            {
                mCurrentSector
                        = std::move(accessRx).assume_value().as_writable();
            }

            mOwner->write_block_header(mCurrentSector);

            return as_span(mCurrentSector).subspan(alloc_map_size);
        }
    };
#if defined(DPLX_COMP_GNUC_AVAILABLE) && !defined(DPLX_COMP_GNUC_EMULATED)
#pragma GCC diagnostic pop
#endif

public:
    index_tree_layout(tree_type &indexTree,
                      block_manager &indexBlocks,
                      detail::tree_position lastAllocated)
        : mIndexTree(indexTree)
        , mIndexBlocks(indexBlocks)
        , mLastAllocated(lastAllocated)
    {
    }

    auto parse(vfilesystem &owner) -> result<void>
    {
        // this simply is peak engineering 😏

        detail::file_descriptor descriptor{};
        vfilesystem_entry entry{};
        tree_stream_position entryPosition{{}, 0};
        VEFS_TRY(entryPosition.sector,
                 mIndexTree.access(detail::tree_position(0)));

        // To write optimal code always start with an infinite loop.
        // -- Alexander Alexandrescu
        for (;;)
        {
            auto const deallocBegin = entryPosition.next_block;

            // find the next used block
            if (auto findRx = find_next_entry(std::move(entryPosition));
                findRx.has_value())
            {
                entryPosition = std::move(findRx).assume_value();
            }
            else if (findRx.assume_error()
                     == archive_errc::sector_reference_out_of_range)
            {
                // dealloc last batch based on mLastAllocated
                auto const endBlock = static_cast<int>(
                        (mLastAllocated.position() + 1) * blocks_per_sector);
                if (endBlock < deallocBegin)
                {
                    return archive_errc::vfilesystem_invalid_size;
                }
                if (endBlock > deallocBegin)
                {
                    VEFS_TRY(mIndexBlocks.dealloc_contiguous(
                            deallocBegin, endBlock - deallocBegin));
                }
                break;
            }
            else
            {
                return std::move(findRx).as_failure();
            }
            auto const deallocAmount = entryPosition.next_block - deallocBegin;

            // dealloc everything in between the last used block and
            // the next one, which might be none
            if (deallocAmount > 0)
            {
                VEFS_TRY(mIndexBlocks.dealloc_contiguous(deallocBegin,
                                                         deallocAmount));
            }

            entry.index_file_position = entryPosition.next_block;
            entry.num_reserved_blocks = -entryPosition.next_block;

            {
                VEFS_TRY(auto &&entryStream,
                         tree_input_stream::open(
                                 &mIndexTree, std::move(entryPosition.sector),
                                 entryPosition.next_block));

                VEFS_TRY(dplx::dp::decode(entryStream, descriptor));

                entryPosition = entryStream.next_block();
            }

            entry.num_reserved_blocks += entryPosition.next_block;

            entry.crypto_ctx
                    = vefs::make_unique_nothrow<detail::file_crypto_ctx>(
                            descriptor.secret, descriptor.secretCounter);
            if (entry.crypto_ctx == nullptr)
            {
                return errc::not_enough_memory;
            }
            entry.tree_info = descriptor.data;

            detail::file_id const id(descriptor.fileId);
            owner.mFiles.insert(id, std::move(entry));

            // #TODO #char8_t convert vfilesystem to u8string
            std::string convertedFilePath(
                    reinterpret_cast<char const *>(descriptor.filePath.data()),
                    descriptor.filePath.size());
            owner.mIndex.insert(std::move(convertedFilePath), id);
        }

        return success();
    }

    auto verify_allocation(tree_type::read_handle sector,
                           std::uint64_t position,
                           int size) -> result<void>
    {
        auto currentPosition = sector.node_position();
        utils::const_bitset_overlay allocMap(
                as_span(sector).first<alloc_map_size>());

        auto const ptr = (position % sector_payload_size - alloc_map_size)
                         / block_size;
        int numBlocks = ptr + utils::div_ceil(size, block_size);

        for (int i = ptr; i < numBlocks; ++i)
        {
            if (i == blocks_per_sector)
            {
                currentPosition = next(currentPosition);
                VEFS_TRY(sector, mIndexTree.access(currentPosition));
                allocMap = utils::const_bitset_overlay{
                        as_span(sector).first<alloc_map_size>()};
                numBlocks -= std::exchange(i, 0);
            }
            if (!allocMap[i])
            {
                return archive_errc::corrupt_index_entry;
            }
        }
        return success();
    }

    auto sync_to_tree(vfilesystem_entry &entry,
                      detail::file_descriptor &descriptor) -> result<void>
    {
        auto const cryptoState = entry.crypto_ctx->state();
        vefs::copy(std::span(cryptoState.secret), std::span(descriptor.secret));
        descriptor.secretCounter = cryptoState.counter;
        descriptor.data = entry.tree_info;
        descriptor.modificationTime = {};

        dplx::dp::void_stream sizeOfDummyStream;
        dplx::dp::emit_context sizeOfCtx{sizeOfDummyStream};
        auto const encodedSize
                = dplx::dp::encoded_size_of(sizeOfCtx, descriptor);
        auto const streamSize
                = dplx::dp::item_size_of_binary(sizeOfCtx, encodedSize);

        auto const neededBlocks
                = static_cast<int>(utils::div_ceil(streamSize, block_size));

        VEFS_TRY(reallocate(entry, neededBlocks));

        VEFS_TRY(auto &&outStream,
                 tree_writer::create(*this, entry.index_file_position,
                                     encodedSize));

        VEFS_TRY(dplx::dp::encode(outStream, descriptor));

        entry.needs_index_update = false;
        return success();
    }

    auto decommission_blocks(int startPos, int numBlocks) -> result<void>
    {
        VEFS_TRY(mIndexBlocks.dealloc_contiguous(startPos, numBlocks));

        while (numBlocks > 0)
        {
            VEFS_TRY(auto &&sector, mIndexTree.access(detail::tree_position(
                                            block_to_tree_position(startPos))));

            write_block_header(sector.as_writable());

            startPos += blocks_per_sector;
            numBlocks -= blocks_per_sector;
        }

        return success();
    }

    [[nodiscard]] auto last_allocated() const noexcept -> detail::tree_position
    {
        return mLastAllocated;
    }

private:
    auto reallocate(vfilesystem_entry &entry, int neededBlocks) -> result<void>
    {
        neededBlocks = std::max(neededBlocks, 1);
        if (entry.num_reserved_blocks == neededBlocks)
        {
            return success();
        }

        auto position = std::exchange(entry.index_file_position, -1);
        auto reserved = std::exchange(entry.num_reserved_blocks, 0);

        if (position >= 0)
        {
            // try to reuse an existing allocation

            auto const diff = neededBlocks - reserved;
            if (diff > 0)
            {
                if (auto extendrx = mIndexBlocks.extend(
                            position, position + reserved - 1, diff))
                {
                    position = extendrx.assume_value();
                }
                else
                {
                    VEFS_TRY(decommission_blocks(position, reserved));
                    position = -1;
                }
            }
            else
            {
                VEFS_TRY(decommission_blocks(position + neededBlocks, -diff));
            }
        }
        if (position < 0)
        {
            auto allocrx = mIndexBlocks.alloc_contiguous(neededBlocks);
            while (!allocrx)
            {
                mLastAllocated = next(mLastAllocated);
                auto firstNewAllocatedBlock
                        = mLastAllocated.position() * blocks_per_sector;

                VEFS_TRY(mIndexTree.access_or_create(mLastAllocated));

                VEFS_TRY(mIndexBlocks.dealloc_contiguous(firstNewAllocatedBlock,
                                                         blocks_per_sector));

                allocrx = mIndexBlocks.alloc_contiguous(neededBlocks);
            }
            position = allocrx.assume_value();
        }
        entry.index_file_position = position;
        entry.num_reserved_blocks = neededBlocks;
        return success();
    }

    // this is awfully inefficient... too bad!
    inline void write_block_header(tree_type::write_handle sector)
    {
        assert(sector);

        auto const begin
                = sector.node_position().position() * blocks_per_sector;

        auto const header = as_span(sector).template subspan<0, block_size>();
        utils::bitset_overlay headerOverlay{header};

        // force the last two (unused) bits to zero
        header.back() = std::byte{};

        mIndexBlocks.write_to_bitset(headerOverlay, begin, blocks_per_sector);
    }

    // NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
    tree_type &mIndexTree;
    block_manager &mIndexBlocks;
    // NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
    detail::tree_position mLastAllocated;
};

vfilesystem::vfilesystem(detail::sector_device &device,
                         detail::archive_sector_allocator &allocator,
                         detail::thread_pool &executor,
                         detail::master_file_info const &info)
    : mDevice(device)
    , mSectorAllocator(allocator)
    , mDeviceExecutor(executor)
    , mCryptoCtx(info.crypto_state)
    , mCommittedRoot(info.tree_info)
    , mIndex(1024U)
    , mFiles(1024U)
    , mIndexBlocks()
    , mIndexTree()
    , mWriteFlag()
    , mIOSync()
{
}

auto vfilesystem::open_existing(detail::sector_device &device,
                                detail::archive_sector_allocator &allocator,
                                detail::thread_pool &executor,
                                detail::master_file_info const &info)
        -> result<std::unique_ptr<vfilesystem>>
{
    std::unique_ptr<vfilesystem> self{
            new (std::nothrow) vfilesystem(device, allocator, executor, info)};

    if (!self)
    {
        return errc::not_enough_memory;
    }

    VEFS_TRY(self->open_existing_impl());

    return self;
}

auto vfilesystem::open_existing_impl() -> result<void>
{
    if (auto openTreeRx = tree_type::open_existing(
                mDevice, mCryptoCtx, mCommittedRoot, mSectorAllocator))
    {
        mIndexTree = std::move(openTreeRx).assume_value();
    }
    else
    {
        return std::move(openTreeRx).as_failure();
    }

    if (mCommittedRoot.maximum_extent == 0
        || mCommittedRoot.maximum_extent
                           % detail::sector_device::sector_payload_size
                   != 0)
    {
        return archive_errc::vfilesystem_invalid_size;
    }

    detail::tree_position lastAllocated(
            detail::lut::sector_position_of(mCommittedRoot.maximum_extent - 1));
    index_tree_layout layout{*mIndexTree, mIndexBlocks, lastAllocated};
    VEFS_TRY(layout.parse(*this));

    return success();
}

auto vfilesystem::create_new(detail::sector_device &device,
                             detail::archive_sector_allocator &allocator,
                             detail::thread_pool &executor,
                             detail::master_file_info const &info)
        -> result<std::unique_ptr<vfilesystem>>
{
    std::unique_ptr<vfilesystem> self{
            new (std::nothrow) vfilesystem(device, allocator, executor, info)};

    if (!self)
    {
        return errc::not_enough_memory;
    }

    VEFS_TRY(self->create_new_impl());

    return self;
}

auto vfilesystem::create_new_impl() -> result<void>
{
    if (auto createTreeRx
        = tree_type::create_new(mDevice, mCryptoCtx, mSectorAllocator))
    {
        mIndexTree = std::move(createTreeRx).assume_value();
    }
    else
    {
        return std::move(createTreeRx).as_failure();
    }

    mCommittedRoot.maximum_extent = detail::sector_device::sector_payload_size;
    VEFS_TRY(mIndexBlocks.dealloc_contiguous(
            0, index_tree_layout::blocks_per_sector));
    mWriteFlag.mark();

    return success();
}

auto vfilesystem::open(const std::string_view filePath,
                       const file_open_mode_bitset mode) -> result<vfile_handle>
{
    using detail::file_id;

    file_id id;
    result<vfile_handle> rx = archive_errc::no_such_vfile;

    if (mIndex.find_fn(filePath, [&id](file_id const &elem) { id = elem; }))
    {
        return open(id);
    }
    if (mode % file_open_mode::create)
    {
        VEFS_TRY(auto &&secrets, mDevice.create_file_secrets());

        VEFS_TRY(auto const fid, file_id::generate());
        rx = vfile::create_new(this, mDeviceExecutor, mSectorAllocator, fid,
                               mDevice, *secrets);
        if (!rx)
        {
            return rx;
        }

        mFiles.insert(fid, vfilesystem_entry{-1,
                                             0,
                                             std::move(secrets),
                                             rx.assume_value(),
                                             false,
                                             {}});

        if (!mIndex.insert(filePath, fid))
        {
            // rollback, someone was faster
            if (auto crx = rx.assume_value()->commit())
            {
                mSectorAllocator.on_leak_detected();
            }

            mFiles.erase(fid);

            rx = open(filePath, mode);
        }
        else
        {
            mWriteFlag.mark();
        }
    }

    return rx;
}

auto vfilesystem::open(detail::file_id const id) -> result<vfile_handle>
{
    result<vfile_handle> rx = archive_errc::no_such_vfile;
    mFiles.update_fn(id, [&](vfilesystem_entry &e) {
        if (auto h = e.instance.lock())
        {
            rx = h;
            return;
        }
        rx = vfile::open_existing(this, mDeviceExecutor, mSectorAllocator, id,
                                  mDevice, *e.crypto_ctx, e.tree_info);
        if (rx)
        {
            e.instance = rx.assume_value();
        }
    });
    return rx;
}

auto vfilesystem::erase(std::string_view filePath) -> result<void>
{
    using detail::file_id;
    using eraser_tree = detail::sector_tree_seq<detail::archive_tree_allocator>;

    file_id id;
    if (!mIndex.find_fn(filePath, [&id](file_id const &elem) { id = elem; }))
    {
        return archive_errc::no_such_vfile;
    }

    bool erased = false;
    vfilesystem_entry victim;
    bool found = mFiles.erase_fn(id, [&](vfilesystem_entry &e) {
        erased = e.instance.expired();
        if (erased)
        {
            victim = std::move(e);
        }
        return erased;
    });

    if (!found)
    {
        return archive_errc::no_such_vfile;
    }
    if (erased)
    {
        mIndex.erase_fn(filePath,
                        [id](file_id const &elem) { return id == elem; });
        mWriteFlag.mark();

        if (victim.index_file_position >= 0)
        {
            detail::tree_position lastAllocated{detail::lut::sector_position_of(
                    mCommittedRoot.maximum_extent - 1)};
            index_tree_layout layout(*mIndexTree, mIndexBlocks, lastAllocated);
            VEFS_TRY(layout.decommission_blocks(victim.index_file_position,
                                                victim.num_reserved_blocks));

            // the file becomes unusable afterwards,
            // therefore we update the index first which prevents
            // us from trying to reparse the file on crash and reopen
            // #TODO properly implement error rollback
            VEFS_TRY(commit());
        }

        // #TODO enqueue on an executor

        VEFS_TRY(auto &&eraser, eraser_tree::open_existing(
                                        mDevice, *victim.crypto_ctx,
                                        victim.tree_info, mSectorAllocator));
        VEFS_TRY(erase_contiguous(*eraser, victim.tree_info.maximum_extent));
        return success();
    }

    return archive_errc::still_in_use;
}

auto vfilesystem::extract(llfio::path_view sourceFilePath,
                          llfio::path_view targetBasePath) -> result<void>
{
    return extract(sourceFilePath, targetBasePath,
                   [this, sourceFilePath]() -> result<vfile_handle> {
                       return open(sourceFilePath.path().string(),
                                   file_open_mode::read);
                   });
}

template <typename OpenFn>
auto vfilesystem::extract(llfio::path_view sourceFilePath,
                          llfio::path_view targetBasePath,
                          OpenFn &&open) -> result<void>
{
    if (sourceFilePath.has_parent_path())
    {
        targetBasePath = llfio::path_view(
                targetBasePath.path().string()
                + sourceFilePath.parent_path().path().string());
        std::error_code errorCode{};
        std::filesystem::create_directories(targetBasePath.path(), errorCode);
        if (errorCode)
        {
            return errorCode;
        }
    }

    VEFS_TRY(auto targetBasePathHandle, llfio::path(targetBasePath));
    VEFS_TRY(auto fileHandle,
             llfio::file(targetBasePathHandle, sourceFilePath.filename(),
                         llfio::file_handle::mode::write,
                         llfio::file_handle::creation::always_new));

    VEFS_TRY(auto &&vfileHandle, open());
    VEFS_TRY(vfileHandle->extract(fileHandle));

    return success();
}

auto vfilesystem::extractAll(llfio::path_view targetBasePath) -> result<void>
{
    for (auto const &indexEntry : mIndex.lock_table())
    {
        VEFS_TRY(extract(indexEntry.first, targetBasePath,
                         [this, &indexEntry]() -> result<vfile_handle> {
                             return open(indexEntry.second);
                         }));
    }
    return success();
}

auto vfilesystem::query(const std::string_view filePath)
        -> result<file_query_result>
{
    using detail::file_id;
    file_id id;
    result<file_query_result> rx = archive_errc::no_such_vfile;
    if (mIndex.find_fn(filePath, [&](file_id const &e) { id = e; }))
    {
        mFiles.find_fn(id, [&](vfilesystem_entry const &e) {
            auto maxExtent = e.tree_info.maximum_extent;
            if (auto h = e.instance.lock())
            {
                maxExtent = h->maximum_extent();
            }

            rx = file_query_result{file_open_mode::readwrite, maxExtent};
        });
    }
    return rx;
}

auto vfilesystem::on_vfile_commit(detail::file_id fileId,
                                  detail::root_sector_info updatedRootInfo)
        -> result<void>
{
    bool found = mFiles.update_fn(fileId, [&](vfilesystem_entry &e) {
        e.needs_index_update = true;
        e.tree_info = updatedRootInfo;
    });
    if (!found)
    {
        return archive_errc::no_such_vfile;
    }
    mWriteFlag.mark();

    return commit();
}

auto vfilesystem::commit() -> result<void>
{
    if (!mWriteFlag.is_dirty())
    {
        return success();
    }

    auto lockedIndex = mIndex.lock_table();

    detail::file_descriptor descriptor;
    detail::tree_position lastAllocated{
            detail::lut::sector_position_of(mCommittedRoot.maximum_extent - 1)};
    index_tree_layout layout(*mIndexTree, mIndexBlocks, lastAllocated);

    for (auto const &[path, fid] : lockedIndex)
    {
        try
        {
            descriptor.fileId = fid.as_uuid();
            auto pathBytes = as_bytes(std::span(path));

            mFiles.update_fn(fid, [&](vfilesystem_entry &e) {
                if (!e.needs_index_update)
                {
                    return;
                }

                // reuse allocation if possible
                descriptor.filePath.resize(pathBytes.size());
                // #TODO #char8_t convert vfilesystem to u8string
                vefs::copy(pathBytes,
                           as_writable_bytes(std::span(descriptor.filePath)));

                if (auto syncrx = layout.sync_to_tree(e, descriptor); !syncrx)
                {
                    syncrx.assume_error()
                            << ed::archive_file{"[archive-index]"};

                    // note that this isn't a child of std::exception
                    throw std::move(syncrx).assume_error();
                }
            });
        }
        catch (system_error::error const &error)
        {
            return error.clone();
        }
        catch (std::bad_alloc const &)
        {
            return errc::not_enough_memory;
        }
    }

    auto maxExtent = (layout.last_allocated().position() + 1)
                     * detail::sector_device::sector_payload_size;
    return mIndexTree->commit(
            [this, maxExtent](detail::root_sector_info rootInfo) noexcept
            -> result<void> { return sync_commit_info(rootInfo, maxExtent); });
}

auto vfilesystem::sync_commit_info(detail::root_sector_info rootInfo,
                                   std::uint64_t maxExtent) noexcept
        -> result<void>
{
    rootInfo.maximum_extent = maxExtent;

    VEFS_TRY_INJECT(mDevice.update_header(mCryptoCtx, rootInfo,
                                          mSectorAllocator.crypto_ctx(), {}),
                    ed::archive_file{"[archive-header]"});

    mCommittedRoot = rootInfo;

    mWriteFlag.unmark();
    return success();
}

auto vfilesystem::recover_unused_sectors() -> result<void>
{
    // #TODO #refactor performance
    using inspection_tree
            = detail::sector_tree_seq<detail::archive_tree_allocator>;
    auto numSectors = mDevice.size();

    std::vector<std::size_t> allocMap(utils::div_ceil(
            numSectors, std::numeric_limits<std::size_t>::digits));

    utils::bitset_overlay allocBits{as_writable_bytes(std::span(allocMap))};

    // precondition the central directory index is currently committed
    {
        VEFS_TRY(auto &&indexTree, inspection_tree::open_existing(
                                           mDevice, mCryptoCtx, mCommittedRoot,
                                           mSectorAllocator));

        VEFS_TRY(indexTree->extract_alloc_map(allocBits));
    }

    auto lockedIndex = mFiles.lock_table();

    for (auto &[id, e] : lockedIndex)
    {
        VEFS_TRY(auto &&tree,
                 inspection_tree::open_existing(mDevice, *e.crypto_ctx,
                                                e.tree_info, mSectorAllocator));

        VEFS_TRY(tree->extract_alloc_map(allocBits));
    }

    for (std::size_t i = 1U; i < numSectors; ++i)
    {
        if (!allocBits[i])
        {
            VEFS_TRY(mSectorAllocator.dealloc_one(detail::sector_id{i}));
        }
    }

    return success();
}

auto vfilesystem::list_files() -> std::vector<std::string>
{
    std::vector<std::string> files;
    for (auto const &indexEntry : mIndex.lock_table())
    {
        std::string_view filename = indexEntry.first;
        files.push_back(std::string(filename));
    }
    return files;
}

auto vfilesystem::validate() -> result<void>
{
    using inspection_tree
            = detail::sector_tree_seq<detail::archive_tree_allocator>;

    auto lockedIndex = mFiles.lock_table();

    for (auto &fileInfo : lockedIndex)
    {
        auto &[id, e] = fileInfo;
        std::unique_ptr<inspection_tree> tree;

        if (auto openrx = inspection_tree::open_existing(
                    mDevice, *e.crypto_ctx, e.tree_info, mSectorAllocator))
        {
            tree = std::move(openrx).assume_value();
        }
        else
        {
            return std::move(openrx).assume_error() << ed::archive_file_id{id};
        }

        std::uint64_t const numSectors
                = utils::div_ceil(e.tree_info.maximum_extent,
                                  detail::sector_device::sector_payload_size);
        for (std::uint64_t i = 1U; i < numSectors; ++i)
        {
            VEFS_TRY_INJECT(tree->move_forward(),
                            ed::archive_file_id{fileInfo.first});
        }
    }

    return success();
}

auto vfilesystem::replace_corrupted_sectors() -> result<void>
{
    using inspection_tree
            = detail::sector_tree_seq<detail::archive_tree_allocator>;

    auto lockedIndex = mFiles.lock_table();

    auto it = lockedIndex.begin();
    auto const end = lockedIndex.end();
    while (it != end)
    {
        auto &[id, e] = *it;
        std::unique_ptr<inspection_tree> tree;

        if (auto openrx = inspection_tree::open_lazy(
                    mDevice, *e.crypto_ctx, e.tree_info, mSectorAllocator))
        {
            tree = std::move(openrx).assume_value();
        }
        else if (openrx.assume_error() == archive_errc::tag_mismatch)
        {
            // corrupted root sector => erase the file
            if (e.index_file_position >= 0)
            {
                detail::tree_position lastAllocated{
                        detail::lut::sector_position_of(
                                mCommittedRoot.maximum_extent - 1)};
                index_tree_layout layout(*mIndexTree, mIndexBlocks,
                                         lastAllocated);

                (void)layout.decommission_blocks(e.index_file_position,
                                                 e.num_reserved_blocks);

                mWriteFlag.mark();
            }
            it = lockedIndex.erase(it);
            mSectorAllocator.on_leak_detected();

            continue;
        }
        else
        {
            return std::move(openrx).assume_error();
        }

        // variable for debugging purposes
        [[maybe_unused]] auto const oldRoot = e.tree_info.root;

        VEFS_TRY_INJECT(tree->move_to(0U, inspection_tree::access_mode::force),
                        ed::archive_file_id{id});

        std::uint64_t const numSectors
                = utils::div_ceil(e.tree_info.maximum_extent,
                                  detail::sector_device::sector_payload_size);
        for (std::uint64_t i = 1U; i < numSectors; ++i)
        {
            VEFS_TRY_INJECT(
                    tree->move_forward(inspection_tree::access_mode::force),
                    ed::archive_file_id{id});
        }

        VEFS_TRY_INJECT(
                tree->commit([this, &it](detail::root_sector_info newRoot) {
                    if (it->second.tree_info != newRoot)
                    {
                        it->second.tree_info = newRoot;
                        it->second.needs_index_update = true;
                        mWriteFlag.mark();
                    }
                }),
                ed::archive_file_id{id});

        std::advance(it, 1);
    }

    lockedIndex.unlock();

    return commit();
}

} // namespace vefs
