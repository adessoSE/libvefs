#include "vfilesystem.hpp"

#include <boost/container/small_vector.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/uuid/random_generator.hpp>

#include <dplx/dp/byte_buffer.hpp>
#include <dplx/dp/item_emitter.hpp>
#include <dplx/dp/stream.hpp>
#include <dplx/dp/streams/chunked_input_stream.hpp>
#include <dplx/dp/streams/chunked_output_stream.hpp>
#include <dplx/dp/streams/memory_input_stream.hpp>
#include <dplx/dp/streams/memory_output_stream.hpp>

#include <vefs/utils/binary_codec.hpp>
#include <vefs/utils/bit.hpp>
#include <vefs/utils/bitset_overlay.hpp>
#include <vefs/utils/random.hpp>

#include "detail/archive_sector_allocator.hpp"
#include "detail/archive_tree_allocator.hpp"
#include "detail/file_descriptor.codec.hpp"
#include "detail/file_descriptor.hpp"
#include "detail/sector_tree_seq.hpp"
#include "platform/sysrandom.hpp"

namespace vefs
{
    class vfilesystem::index_tree_layout
    {
    public:
        static constexpr inline auto sector_payload_size =
            detail::sector_device::sector_payload_size;

        static constexpr std::uint64_t block_size = 64u;
        static constexpr std::uint64_t alloc_map_size = 64u;
        static constexpr auto blocks_per_sector =
            (sector_payload_size - alloc_map_size) / block_size;

        static_assert(
            alloc_map_size *
                std::numeric_limits<std::underlying_type_t<std::byte>>::digits >
            blocks_per_sector);

    private:
        static constexpr std::uint64_t block_to_tree_position(const int block)
        {
            return static_cast<std::uint64_t>(block) / blocks_per_sector;
        }

        static constexpr std::uint64_t block_to_file_position(const int block)
        {
            const auto wblock = static_cast<std::uint64_t>(block);

            const auto treePosition = wblock / blocks_per_sector;
            const auto treeOffset = wblock % blocks_per_sector;

            return treePosition * sector_payload_size + alloc_map_size +
                   treeOffset * block_size;
        }

        using map_bucket_type = std::size_t;
        static constexpr auto map_bucket_size =
            std::numeric_limits<map_bucket_type>::digits;
        static constexpr auto map_buckets_per_sector =
            alloc_map_size / sizeof(map_bucket_type);

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
                map_bucket_type eblock = endian_load<
                    map_bucket_type, sizeof(map_bucket_type), order::little>(
                    reinterpret_cast<unsigned char const *>(
                        allocMap.data() + offset * sizeof(map_bucket_type)));

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
                    return offset * map_bucket_size + start +
                           utils::countr_zero(eblock);
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

        class tree_input_stream final
            : public dplx::dp::chunked_input_stream_base<tree_input_stream>
        {
            friend class dplx::dp::chunked_input_stream_base<tree_input_stream>;
            using base_type =
                dplx::dp::chunked_input_stream_base<tree_input_stream>;

            tree_type *mTree;
            tree_type::read_handle mCurrentSector;

            struct stream_info
            {
                unsigned int prefix_size;
                std::uint32_t stream_size;
            };

            explicit tree_input_stream(
                std::span<std::byte const> const initialReadArea,
                std::uint64_t streamSize, tree_type *tree,
                tree_type::read_handle currentSector) noexcept
                : base_type(initialReadArea, streamSize)
                , mTree(tree)
                , mCurrentSector(std::move(currentSector))
            {
            }

        public:
            static auto open(tree_type *tree,
                             tree_type::read_handle initialSector,
                             int blockOffset) noexcept
                -> result<tree_input_stream>
            {
                std::span<std::byte const> sectorContent =
                    as_span(initialSector);

                auto const nextUnoccupied = find_next<unoccupied>(
                    sectorContent.first<alloc_map_size>(), blockOffset);

                auto const numAvailableBlocks = nextUnoccupied - blockOffset;
                auto const maxChunkSize =
                    static_cast<std::uint32_t>(numAvailableBlocks * block_size);

                sectorContent = sectorContent.subspan(
                    alloc_map_size + blockOffset * block_size, maxChunkSize);

                DPLX_TRY(auto streamInfo,
                         parse_stream_prefix(sectorContent.data()));

                auto const initialChunkSize =
                    std::min(streamInfo.stream_size,
                             maxChunkSize - streamInfo.prefix_size);
                sectorContent = sectorContent.subspan(streamInfo.prefix_size,
                                                      initialChunkSize);

                return tree_input_stream(sectorContent, streamInfo.stream_size,
                                         tree, std::move(initialSector));
            }

            auto next_block() const noexcept -> tree_stream_position
            {
                auto const state = current_read_area();
                auto const sectorContentBegin = as_span(mCurrentSector).data();

                auto const blockOffset = state.remaining_begin() -
                                         sectorContentBegin - alloc_map_size;
                auto const nextBlock =
                    static_cast<int>(utils::div_ceil(blockOffset, block_size));

                return {.sector = mCurrentSector, .next_block = nextBlock};
            }

        private:
            static auto parse_stream_prefix(std::byte const *data)
                -> dplx::dp::result<stream_info>
            {
                using namespace dplx;

                auto streamInfo = dp::detail::parse_item_info_speculative(data);

                if (streamInfo.code != dp::detail::decode_errc::nothing)
                {
                    return static_cast<dp::errc>(streamInfo.code);
                }
                if (std::byte{streamInfo.type} != dp::type_code::binary)
                {
                    return dplx::dp::errc::item_type_mismatch;
                }
                if (streamInfo.value >
                    std::numeric_limits<std::uint32_t>::max())
                {
                    return dplx::dp::errc::item_value_out_of_range;
                }

                return stream_info{.prefix_size = static_cast<unsigned int>(
                                       streamInfo.encoded_length),
                                   .stream_size = static_cast<std::uint32_t>(
                                       streamInfo.value)};
            }

            auto acquire_next_chunk_impl(std::uint64_t remaining) noexcept
                -> dplx::dp::result<dplx::dp::const_byte_buffer_view>
            {
                auto const currentPosition = mCurrentSector.node_position();
                auto const nextPosition = next(currentPosition);

                if (auto accessRx = mTree->access(nextPosition);
                    accessRx.has_failure())
                    DPLX_ATTR_UNLIKELY
                    {
                        if (accessRx.assume_error() ==
                            archive_errc::sector_reference_out_of_range)
                        {
                            return dplx::dp::errc::end_of_stream;
                        }

                        // #TODO implement underlying error forwarding
                        return dplx::dp::errc::bad;
                    }
                else
                    DPLX_ATTR_LIKELY
                    {
                        mCurrentSector = std::move(accessRx).assume_value();
                    }

                std::span<std::byte const> const memory =
                    as_span(mCurrentSector);

                auto const firstUnallocated =
                    find_next<unoccupied>(memory.first<alloc_map_size>(), 0);

                auto const nextChunkSize =
                    std::min(remaining, blocks_per_sector * block_size);
                if (firstUnallocated <
                    utils::div_ceil(nextChunkSize, block_size))
                {
                    return dplx::dp::errc::end_of_stream;
                }

                return dplx::dp::const_byte_buffer_view(
                    memory.subspan(alloc_map_size, nextChunkSize));
            }
        };
        static_assert(dplx::dp::input_stream<tree_input_stream>);

        auto find_next_entry(tree_stream_position begin) noexcept
            -> result<tree_stream_position>
        {
            if (begin.next_block < blocks_per_sector)
            {
                std::span<std::byte const> const sectorContent =
                    as_span(begin.sector);
                begin.next_block = find_next<occupied>(
                    sectorContent.first<alloc_map_size>(), begin.next_block);
            }

            while (begin.next_block >= blocks_per_sector)
            {
                auto const nextPosition = next(begin.sector.node_position());

                DPLX_TRY(begin.sector, mIndexTree.access(nextPosition));

                std::span<std::byte const> const sectorContent =
                    as_span(begin.sector);
                begin.next_block = find_next<occupied>(
                    sectorContent.first<alloc_map_size>(), 0u);
            }

            return begin;
        }

        class tree_writer final
            : public dplx::dp::chunked_output_stream_base<tree_writer>
        {
            friend class dplx::dp::chunked_output_stream_base<tree_writer>;
            using base_type = dplx::dp::chunked_output_stream_base<tree_writer>;

            index_tree_layout *mOwner;
            tree_type::write_handle mCurrentSector;

            explicit tree_writer(index_tree_layout &owner,
                                 tree_type::write_handle &&currentSector,
                                 std::uint64_t inSectorOffset,
                                 std::uint64_t size)
                : base_type(as_span(currentSector).subspan(inSectorOffset),
                            size - inSectorOffset)
                , mOwner(&owner)
                , mCurrentSector(std::move(currentSector))
            {
            }

            static auto
            write_byte_stream_prefix(tree_type::write_handle &handle,
                                     std::uint64_t offset, std::uint32_t size)
                -> dplx::dp::result<int>
            {
                using emit = dplx::dp::item_emitter<dplx::dp::byte_buffer_view>;

                dplx::dp::byte_buffer_view buffer(
                    std::span(as_span(handle)).subspan(offset, block_size));

                DPLX_TRY(emit::binary(buffer, size));

                return buffer.consumed_size();
            }

        public:
            static auto create(index_tree_layout &owner, int firstBlock,
                               int encodedSize) -> result<tree_writer>
            {
                auto const offset = block_to_file_position(firstBlock);
                auto const size = static_cast<std::uint64_t>(encodedSize);

                auto firstPosition = detail::lut::sector_position_of(offset);
                auto inSectorOffset =
                    offset -
                    firstPosition * detail::sector_device::sector_payload_size;

                VEFS_TRY(firstSector,
                         owner.mIndexTree.access(
                             detail::tree_position(firstPosition)));

                tree_type::write_handle writeHandle(std::move(firstSector));
                owner.write_block_header(writeHandle);
                VEFS_TRY(prefixSize,
                         write_byte_stream_prefix(writeHandle, inSectorOffset,
                                                  encodedSize));

                return tree_writer(owner, std::move(writeHandle),
                                   inSectorOffset + prefixSize, size);
            }

        private:
            auto acquire_next_chunk_impl() noexcept
                -> dplx::dp::result<std::span<std::byte>>
            {
                auto const nextPosition = next(mCurrentSector.node_position());

                if (auto accessRx = mOwner->mIndexTree.access(nextPosition);
                    accessRx.has_failure())
                    DPLX_ATTR_UNLIKELY
                    {
                        // #TODO implement underlying error forwarding
                        return dplx::dp::errc::bad;
                    }
                else
                    DPLX_ATTR_LIKELY
                    {
                        mCurrentSector = tree_type::write_handle(
                            std::move(accessRx).assume_value());
                    }

                mOwner->write_block_header(mCurrentSector);

                return std::span<std::byte>(
                    as_span(mCurrentSector).subspan(alloc_map_size));
            }
        };

    public:
        index_tree_layout(tree_type &indexTree, block_manager &indexBlocks,
                          detail::tree_position lastAllocated)
            : mIndexTree(indexTree)
            , mIndexBlocks(indexBlocks)
            , mLastAllocated(lastAllocated)
        {
        }

        auto parse(vfilesystem &owner) -> result<void>
        {
            // this simply is peak engineering 😏

            detail::file_descriptor descriptor;
            vfilesystem_entry entry;
            tree_stream_position entryPosition{{}, 0};
            DPLX_TRY(entryPosition.sector,
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
                else if (findRx.assume_error() ==
                         archive_errc::sector_reference_out_of_range)
                {
                    // dealloc last batch based on mLastAllocated
                    auto const endBlock =
                        (mLastAllocated.position() + 1) * blocks_per_sector;
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
                auto const deallocAmount =
                    entryPosition.next_block - deallocBegin;

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
                    VEFS_TRY(entryStream,
                             tree_input_stream::open(
                                 &mIndexTree, std::move(entryPosition.sector),
                                 entryPosition.next_block));

                    VEFS_TRY(dplx::dp::decode(entryStream, descriptor));

                    entryPosition = entryStream.next_block();
                }

                entry.num_reserved_blocks += entryPosition.next_block;

                entry.crypto_ctx.reset(
                    new (std::nothrow) detail::file_crypto_ctx(
                        descriptor.secret, descriptor.secretCounter));
                if (!entry.crypto_ctx)
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
                               std::uint64_t position, int size) -> result<void>
        {
            auto currentPosition = sector.node_position();
            utils::const_bitset_overlay allocMap(
                as_span(sector).first<alloc_map_size>());

            const auto ptr =
                (position % sector_payload_size - alloc_map_size) / block_size;
            int numBlocks = ptr + utils::div_ceil(size, block_size);

            for (int i = ptr; i < numBlocks; ++i)
            {
                if (i == blocks_per_sector)
                {
                    currentPosition = next(currentPosition);
                    VEFS_TRY(nextSector, mIndexTree.access(currentPosition));
                    sector = std::move(nextSector);
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
            vefs::copy(std::span(cryptoState.secret),
                       std::span(descriptor.secret));
            descriptor.secretCounter = cryptoState.counter;
            descriptor.data = entry.tree_info;
            descriptor.modificationTime = {};

            auto const encodedSize = dplx::dp::encoded_size_of(descriptor);
            auto const streamSize =
                encodedSize + dplx::dp::encoded_size_of(encodedSize);

            auto const neededBlocks =
                static_cast<int>(utils::div_ceil(encodedSize, block_size));

            VEFS_TRY(reallocate(entry, neededBlocks));

            VEFS_TRY(outStream,
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
                VEFS_TRY(sector, mIndexTree.access(detail::tree_position(
                                     block_to_tree_position(startPos))));

                write_block_header(tree_type::write_handle{sector});

                startPos += blocks_per_sector;
                numBlocks -= blocks_per_sector;
            }

            return success();
        }

        auto last_allocated() const noexcept -> detail::tree_position
        {
            return mLastAllocated;
        }

    private:
        auto reallocate(vfilesystem_entry &entry, int neededBlocks)
            -> result<void>
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

                const auto diff = neededBlocks - reserved;
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
                    VEFS_TRY(
                        decommission_blocks(position + neededBlocks, -diff));
                }
            }
            if (position < 0)
            {
                auto allocrx = mIndexBlocks.alloc_contiguous(neededBlocks);
                while (!allocrx)
                {
                    mLastAllocated = next(mLastAllocated);
                    auto firstNewAllocatedBlock =
                        mLastAllocated.position() * blocks_per_sector;

                    VEFS_TRY(mIndexTree.access_or_create(mLastAllocated));

                    VEFS_TRY(mIndexBlocks.dealloc_contiguous(
                        firstNewAllocatedBlock, blocks_per_sector));

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

            const auto begin =
                sector.node_position().position() * blocks_per_sector;

            const auto header =
                as_span(sector).template subspan<0, block_size>();
            utils::bitset_overlay headerOverlay{header};

            // force the last two (unused) bits to zero
            header.back() = std::byte{};

            mIndexBlocks.write_to_bitset(headerOverlay, begin,
                                         blocks_per_sector);
        }

        tree_type &mIndexTree;
        block_manager &mIndexBlocks;
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
        , mIndex()
        , mFiles()
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

        return std::move(self);
    }

    auto vfilesystem::open_existing_impl() -> result<void>
    {
        if (auto openTreeRx =
                tree_type::open_existing(mDevice, mCryptoCtx, mDeviceExecutor,
                                         mCommittedRoot, mSectorAllocator))
        {
            mIndexTree = std::move(openTreeRx).assume_value();
        }
        else
        {
            return std::move(openTreeRx).as_failure();
        }

        if (mCommittedRoot.maximum_extent == 0 ||
            mCommittedRoot.maximum_extent %
                    detail::sector_device::sector_payload_size !=
                0)
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

        return std::move(self);
    }

    auto vfilesystem::create_new_impl() -> result<void>
    {
        if (auto createTreeRx = tree_type::create_new(
                mDevice, mCryptoCtx, mDeviceExecutor, mSectorAllocator))
        {
            mIndexTree = std::move(createTreeRx).assume_value();
        }
        else
        {
            return std::move(createTreeRx).as_failure();
        }

        mCommittedRoot.maximum_extent =
            detail::sector_device::sector_payload_size;
        VEFS_TRY(mIndexBlocks.dealloc_contiguous(
            0, index_tree_layout::blocks_per_sector));
        mWriteFlag.mark();

        return success();
    }

    auto vfilesystem::open(const std::string_view filePath,
                           const file_open_mode_bitset mode)
        -> result<vfile_handle>
    {
        using detail::file_id;

        file_id id;
        result<vfile_handle> rx = archive_errc::no_such_file;

        if (mIndex.find_fn(filePath, [&id](const file_id &elem) { id = elem; }))
        {
            if (mFiles.update_fn(id, [&](vfilesystem_entry &e) {
                    if (auto h = e.instance.lock())
                    {
                        rx = h;
                        return;
                    }
                    rx = vfile::open_existing(this, mDeviceExecutor,
                                              mSectorAllocator, id, mDevice,
                                              *e.crypto_ctx, e.tree_info);
                    if (rx)
                    {
                        e.instance = rx.assume_value();
                    }
                }))
            {
                return rx;
            }
        }
        if (mode % file_open_mode::create)
        {
            VEFS_TRY(secrets, mDevice.create_file_secrets());

            thread_local utils::xoroshiro128plus fileid_prng = []() {
                std::array<std::uint64_t, 2> randomState{};
                auto rx = random_bytes(as_writable_bytes(span(randomState)));
                if (!rx)
                {
                    throw error_exception{rx.assume_error()};
                }

                return utils::xoroshiro128plus{randomState[0], randomState[1]};
            }();
            thread_local boost::uuids::basic_random_generator generate_fileid{
                fileid_prng};

            file_id fid{generate_fileid()};
            rx = vfile::create_new(this, mDeviceExecutor, mSectorAllocator, fid,
                                   mDevice, *secrets);
            if (!rx)
            {
                return rx;
            }

            mFiles.insert(
                fid,
                vfilesystem_entry{
                    -1, 0, std::move(secrets), rx.assume_value(), true, {}});

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

    auto vfilesystem::erase(std::string_view filePath) -> result<void>
    {
        using detail::file_id;
        using eraser_tree =
            detail::sector_tree_seq<detail::archive_tree_allocator>;

        file_id id;
        if (!mIndex.find_fn(filePath,
                            [&id](const file_id &elem) { id = elem; }))
        {
            return archive_errc::no_such_file;
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
            return archive_errc::no_such_file;
        }
        else if (erased)
        {
            mIndex.erase_fn(filePath,
                            [id](const file_id &elem) { return id == elem; });
            mWriteFlag.mark();

            if (victim.index_file_position >= 0)
            {
                detail::tree_position lastAllocated{
                    detail::lut::sector_position_of(
                        mCommittedRoot.maximum_extent - 1)};
                index_tree_layout layout(*mIndexTree, mIndexBlocks,
                                         lastAllocated);
                VEFS_TRY(layout.decommission_blocks(
                    victim.index_file_position, victim.num_reserved_blocks));

                // the file becomes unusable afterwards,
                // therefore we update the index first which prevents
                // us from trying to reparse the file on crash and reopen
                // #TODO properly implement error rollback
                VEFS_TRY(commit());
            }

            // #TODO enqueue on an executor

            VEFS_TRY(eraser, eraser_tree::open_existing(
                                 mDevice, *victim.crypto_ctx, victim.tree_info,
                                 mSectorAllocator));
            VEFS_TRY(
                erase_contiguous(*eraser, victim.tree_info.maximum_extent));
            return success();
        }
        else
        {
            return errc::still_in_use;
        }
    }

    auto vfilesystem::query(const std::string_view filePath)
        -> result<file_query_result>
    {
        using detail::file_id;
        file_id id;
        result<file_query_result> rx = archive_errc::no_such_file;
        if (mIndex.find_fn(filePath, [&](const file_id &e) { id = e; }))
        {
            mFiles.find_fn(id, [&](const vfilesystem_entry &e) {
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
            return archive_errc::no_such_file;
        }
        mWriteFlag.mark();
        return success();
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

        for (const auto &[path, fid] : lockedIndex)
        {
            try
            {
                descriptor.fileId = fid.as_uuid();
                auto pathBytes = as_bytes(span(path));

                mFiles.update_fn(fid, [&](vfilesystem_entry &e) {
                    if (!e.needs_index_update)
                    {
                        return;
                    }

                    // reuse allocation if possible
                    descriptor.filePath.resize(pathBytes.size());
                    // #TODO #char8_t convert vfilesystem to u8string
                    vefs::copy(pathBytes,
                               as_writable_bytes(span(descriptor.filePath)));

                    if (auto syncrx = layout.sync_to_tree(e, descriptor);
                        !syncrx)
                    {
                        syncrx.assume_error()
                            << ed::archive_file{"[archive-index]"};
                        throw error_exception(std::move(syncrx).assume_error());
                    }
                });
            }
            catch (const error_exception &exc)
            {
                return exc.error();
            }
            catch (const std::bad_alloc &)
            {
                return errc::not_enough_memory;
            }
        }

        auto maxExtent = (layout.last_allocated().position() + 1) *
                         detail::sector_device::sector_payload_size;
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
                                              mSectorAllocator.crypto_ctx(),
                                              {}),
                        ed::archive_file{"[archive-header]"});

        mCommittedRoot = rootInfo;

        mWriteFlag.unmark();
        return success();
    }

    auto vfilesystem::recover_unused_sectors() -> result<void>
    {
        // #TODO #refactor performance
        using inspection_tree =
            detail::sector_tree_seq<detail::archive_tree_allocator>;
        auto numSectors = mDevice.size();

        std::vector<std::size_t> allocMap(utils::div_ceil(
            numSectors, std::numeric_limits<std::size_t>::digits));

        utils::bitset_overlay allocBits{as_writable_bytes(span(allocMap))};

        // precondition the central directory index is currently committed
        {
            VEFS_TRY(indexTree, inspection_tree::open_existing(
                                    mDevice, mCryptoCtx, mCommittedRoot,
                                    mSectorAllocator));

            VEFS_TRY(indexTree->extract_alloc_map(allocBits));
        }

        auto lockedIndex = mFiles.lock_table();

        for (auto &[id, e] : lockedIndex)
        {
            VEFS_TRY(tree, inspection_tree::open_existing(
                               mDevice, *e.crypto_ctx, e.tree_info,
                               mSectorAllocator));

            VEFS_TRY(tree->extract_alloc_map(allocBits));
        }

        for (std::size_t i = 1; i < numSectors; ++i)
        {
            if (!allocBits[i])
            {
                VEFS_TRY(mSectorAllocator.dealloc_one(detail::sector_id{i}));
            }
        }

        return success();
    }
} // namespace vefs
