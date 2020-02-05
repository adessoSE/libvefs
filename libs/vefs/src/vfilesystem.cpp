#include "vfilesystem.hpp"

#include <boost/container/small_vector.hpp>
#include <boost/uuid/random_generator.hpp>
#include <google/protobuf/io/zero_copy_stream.h>

#include <vefs/utils/binary_codec.hpp>
#include <vefs/utils/bit.hpp>
#include <vefs/utils/bitset_overlay.hpp>
#include <vefs/utils/random.hpp>

#include "detail/archive_sector_allocator.hpp"
#include "detail/archive_tree_allocator.hpp"
#include "detail/proto-helper.hpp"
#include "detail/sector_tree_seq.hpp"
#include "platform/sysrandom.hpp"

namespace vefs
{
    void pack(vfilesystem_entry &entry, adesso::vefs::FileDescriptor &fd)
    {
        entry.crypto_ctx->pack_to(fd);
        entry.tree_info.pack_to(fd);
    }
    void unpack(vfilesystem_entry &entry, adesso::vefs::FileDescriptor &fd)
    {
        ro_blob<32> secret{
            as_bytes(span(fd.filesecret())).template first<32>()};
        ro_blob<16> counter{
            as_bytes(span(fd.filesecretcounter())).template first<16>()};

        entry.crypto_ctx = std::make_unique<detail::file_crypto_ctx>(
            secret, crypto::counter{counter});
        entry.tree_info = detail::root_sector_info::unpack_from(fd);
    }

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

        class protobuf_in_buffer final
            : public google::protobuf::io::ZeroCopyInputStream
        {
        public:
            protobuf_in_buffer(index_tree_layout &owner, std::uint64_t start,
                               tree_type::read_handle startSector)
                : google::protobuf::io::ZeroCopyInputStream()
                , mOwner(owner)
                , mCurrentSector(std::move(startSector))
                , mPosition(start)
                , mRemaining(std::numeric_limits<std::uint64_t>::max())
                , mByteCount(0)
            {
                if (mCurrentSector)
                {
                    const auto offset = mPosition % sector_payload_size;
                    const auto buffer = as_span(mCurrentSector);

                    mRemaining = load_primitive<std::uint16_t>(buffer, offset);
                    mPosition += sizeof(std::uint16_t);
                }
            }

            auto current_sector() -> tree_type::read_handle
            {
                return mCurrentSector;
            }

            auto last_block_index() -> std::uint64_t
            {
                const auto lastOffset =
                    (mPosition + mRemaining - 1) % sector_payload_size;

                return (lastOffset - alloc_map_size) / block_size;
            }

        private:
            auto validate_allocation() -> result<void>
            {
                utils::const_bitset_overlay allocMap(
                    as_span(mCurrentSector).first<alloc_map_size>());

                const int ptr =
                    (mPosition % sector_payload_size - alloc_map_size) /
                    block_size;
                const int numBlocks =
                    std::min(ptr + utils::div_ceil(mRemaining, block_size),
                             blocks_per_sector);

                // #TODO efficiency
                for (int i = ptr; i < numBlocks; ++i)
                {
                    if (!allocMap[i])
                    {
                        return archive_errc::corrupt_index_entry;
                    }
                }
                return success();
            }

            // Inherited via ZeroCopyInputStream
            virtual bool Next(const void **data, int *size) override
            {
                using detail::lut::sector_position_of;

                if (mRemaining == 0)
                {
                    return false;
                }

                auto offset = mPosition % sector_payload_size;
                if (auto nextPosition = sector_position_of(mPosition);
                    !mCurrentSector ||
                    mCurrentSector.node_position().position() != nextPosition)
                {
                    if (auto accessrx = mOwner.mIndexTree.access(
                            detail::tree_position(nextPosition)))
                    {
                        mCurrentSector = std::move(accessrx).assume_value();
                    }
                    else
                    {
                        return false;
                    }

                    if (offset == 0)
                    {
                        // on wrap around we need to skip the alloc map
                        mPosition += alloc_map_size;
                        offset = alloc_map_size;
                    }
                    if (mRemaining == std::numeric_limits<std::uint64_t>::max())
                    {
                        const auto buffer = as_span(mCurrentSector);

                        mRemaining =
                            load_primitive<std::uint16_t>(buffer, offset);
                        mPosition += sizeof(std::uint16_t);
                        offset += sizeof(std::uint16_t);
                    }
                    if (!validate_allocation())
                    {
                        return false;
                    }
                }
                const auto bufferLimit =
                    std::min(mRemaining, sector_payload_size - offset);
                auto buffer =
                    as_span(mCurrentSector).subspan(offset, bufferLimit);

                *data = buffer.data();
                *size = buffer.size();

                mPosition += bufferLimit;
                mRemaining -= bufferLimit;
                mByteCount += bufferLimit;

                return true;
            }
            virtual void BackUp(int count) override
            {
                mPosition -= count;
                mRemaining += count;
                mByteCount -= count;
            }
            virtual bool Skip(int count) override
            {
                auto rcount = std::min<std::uint64_t>(count, mRemaining);
                mPosition += rcount;
                mRemaining -= rcount;
                mByteCount += rcount;

                return mRemaining != 0;
            }
            virtual int64_t ByteCount() const override
            {
                return mByteCount;
            }

            index_tree_layout &mOwner;
            tree_type::read_handle mCurrentSector;
            std::uint64_t mPosition;
            std::uint64_t mRemaining;
            std::int64_t mByteCount;
        };

        class protobuf_out_buffer final
            : public google::protobuf::io::ZeroCopyOutputStream
        {
        public:
            protobuf_out_buffer(index_tree_layout &owner, std::uint64_t start,
                                std::uint64_t size)
                : google::protobuf::io::ZeroCopyOutputStream()
                , mOwner(owner)
                , mLastError()
                , mCurrentSector()
                , mCurrentBuffer()
                , mPosition(start)
                , mRemaining(size)
                , mByteCount(0)
            {
            }

            auto last_error() -> const error &
            {
                return mLastError;
            }

        private:
            // Inherited via ZeroCopyOutputStream
            virtual bool Next(void **data, int *size) override
            {
                using detail::lut::sector_position_of;

                if (mRemaining == 0)
                {
                    mLastError = errc::resource_exhausted;
                    mLastError << ed::archive_file_write_area{
                        {mPosition - mByteCount, mPosition}};
                    return false;
                }

                const auto offset = mPosition % sector_payload_size;
                if (auto nextPosition = sector_position_of(mPosition);
                    !mCurrentSector ||
                    mCurrentSector.node_position().position() != nextPosition)
                {
                    if (auto accessrx = mOwner.mIndexTree.access(
                            detail::tree_position(nextPosition)))
                    {
                        mCurrentSector = tree_type::write_handle(
                            std::move(accessrx).assume_value());
                    }
                    else
                    {
                        mLastError = errc::resource_exhausted;
                        mLastError << ed::archive_file_write_area{
                            {mPosition - mByteCount, mPosition + mRemaining}};
                        return false;
                    }
                    mOwner.write_block_header(mCurrentSector);
                }
                const auto bufferLimit =
                    std::min(mRemaining, sector_payload_size - offset);
                mCurrentBuffer =
                    as_span(mCurrentSector).subspan(offset, bufferLimit);

                *data = mCurrentBuffer.data();
                *size = mCurrentBuffer.size();

                mPosition += bufferLimit;
                mRemaining -= bufferLimit;
                mByteCount += bufferLimit;

                return true;
            }
            virtual void BackUp(int count) override
            {
                fill_blob(mCurrentBuffer.last(count));
                mPosition -= count;
                mRemaining += count;
                mByteCount -= count;
            }
            virtual int64_t ByteCount() const override
            {
                return mByteCount;
            }

            index_tree_layout &mOwner;
            error mLastError;
            tree_type::write_handle mCurrentSector;
            rw_dynblob mCurrentBuffer;
            std::uint64_t mPosition;
            std::uint64_t mRemaining;
            std::int64_t mByteCount;
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
            // #refactor this simply isn't peak engineering

            adesso::vefs::FileDescriptor descriptor;
            vfilesystem_entry entry;
            detail::tree_position currentPosition(0);
            VEFS_TRY(currentSector, mIndexTree.access(currentPosition));

            for (;;)
            {
                utils::const_bitset_overlay overlay(
                    as_span(currentSector).first<alloc_map_size>());

                auto indexOffset =
                    currentPosition.position() * blocks_per_sector;

                for (int i = 0; i < blocks_per_sector; ++i)
                {
                    if (!overlay[i])
                    {
                        VEFS_TRY(mIndexBlocks.dealloc_one(indexOffset + i));
                        continue;
                    }

                    entry.needs_index_update = false;
                    entry.index_file_position = indexOffset + i;

                    const auto bufferStart =
                        currentPosition.position() * sector_payload_size +
                        alloc_map_size + i * block_size;
                    protobuf_in_buffer entryBuffer(*this, bufferStart,
                                                   std::move(currentSector));

                    if (!descriptor.ParseFromZeroCopyStream(&entryBuffer))
                    {
                        return archive_errc::corrupt_index_entry;
                    }
                    utils::scope_guard eraseGuard = [&]() {
                        erase_secrets(descriptor);
                    };

                    // #TODO validate
                    unpack(entry, descriptor);

                    currentSector = entryBuffer.current_sector();
                    i = entryBuffer.last_block_index();
                    if (const auto updatedPosition =
                            currentSector.node_position();
                        currentPosition != updatedPosition)
                    {
                        indexOffset =
                            updatedPosition.position() * blocks_per_sector;
                        currentPosition = updatedPosition;
                    }

                    entry.num_reserved_blocks =
                        indexOffset + i + 1 - entry.index_file_position;

                    detail::file_id id{
                        as_bytes(span(descriptor.fileid())).first<16>()};
                    owner.mFiles.insert(id, std::move(entry));
                    owner.mIndex.insert(
                        std::move(*descriptor.mutable_filepath()), id);
                }

                mLastAllocated = currentPosition;
                currentPosition.position(currentPosition.position() + 1);
                if (auto accessrx = mIndexTree.access(currentPosition))
                {
                    currentSector = std::move(accessrx).assume_value();
                }
                else if (accessrx.assume_error() !=
                         archive_errc::sector_reference_out_of_range)
                {
                    return std::move(accessrx).as_failure();
                }
                else
                {
                    break;
                }
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
                    currentPosition.position(currentPosition.position() + 1);
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
                          adesso::vefs::FileDescriptor &descriptor)
            -> result<void>
        {
            pack(entry, descriptor);

            const std::size_t size = descriptor.ByteSizeLong();
            assert(size <= std::numeric_limits<std::uint16_t>::max());

            constexpr auto prefixSize = sizeof(std::uint16_t);
            const auto fullSize = size + prefixSize;

            const auto neededBlocks =
                static_cast<int>(utils::div_ceil(fullSize, block_size));
            const auto reservedSpace = (neededBlocks * block_size) - prefixSize;

            VEFS_TRY(reallocate(entry, neededBlocks));

            const auto start =
                block_to_file_position(entry.index_file_position);
            {
                const auto prefix = static_cast<std::uint16_t>(size);
                VEFS_TRY(write(mIndexTree, ro_blob_cast(prefix), start));
            }

            protobuf_out_buffer vBuffer(*this, start + prefixSize,
                                        reservedSpace);

            if (!descriptor.SerializeToZeroCopyStream(&vBuffer))
            {
                error e{archive_errc::vfilesystem_entry_serialization_failed};
                if (vBuffer.last_error())
                {
                    e << ed::wrapped_error{vBuffer.last_error()};
                }
                return std::move(e);
            }
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
                    mLastAllocated =
                        detail::tree_position(mLastAllocated.position() + 1);
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
                             detail::master_file_info &info)
        : mDevice(device)
        , mInfo(info)
        , mSectorAllocator(allocator)
        , mDeviceExecutor(executor)
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
                                    detail::master_file_info &info)
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
        if (auto openTreeRx = tree_type::open_existing(
                mDevice, mInfo.crypto_ctx, mDeviceExecutor, mInfo.tree_info,
                mSectorAllocator))
        {
            mIndexTree = std::move(openTreeRx).assume_value();
        }
        else
        {
            return std::move(openTreeRx).as_failure();
        }

        // #TODO remove backward compat
        if (/*mInfo.tree_info.maximum_extent == 0 ||*/
            mInfo.tree_info.maximum_extent %
                detail::sector_device::sector_payload_size !=
            0)
        {
            return archive_errc::vfilesystem_invalid_size;
        }

        detail::tree_position lastAllocated(detail::lut::sector_position_of(
            mInfo.tree_info.maximum_extent - 1));
        index_tree_layout layout{*mIndexTree, mIndexBlocks, lastAllocated};
        VEFS_TRY(layout.parse(*this));

        // #TODO remove backward compat
        if (mInfo.tree_info.maximum_extent == 0)
        {
            mInfo.tree_info.maximum_extent =
                layout.last_allocated().position() *
                detail::sector_device::sector_payload_size;
        }

        return success();
    }

    auto vfilesystem::create_new(detail::sector_device &device,
                                 detail::archive_sector_allocator &allocator,
                                 detail::thread_pool &executor,
                                 detail::master_file_info &info)
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
                mDevice, mInfo.crypto_ctx, mDeviceExecutor, mSectorAllocator))
        {
            mIndexTree = std::move(createTreeRx).assume_value();
        }
        else
        {
            return std::move(createTreeRx).as_failure();
        }

        mInfo.tree_info.maximum_extent =
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
                        mInfo.tree_info.maximum_extent - 1)};
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

        adesso::vefs::FileDescriptor descriptor;
        detail::tree_position lastAllocated{detail::lut::sector_position_of(
            mInfo.tree_info.maximum_extent - 1)};
        index_tree_layout layout(*mIndexTree, mIndexBlocks, lastAllocated);

        for (const auto &[path, fid] : lockedIndex)
        {
            try
            {
                descriptor.set_filepath(path);
                auto ufid = fid.as_uuid();
                span sfid(ufid.data);
                descriptor.set_fileid(sfid.data(), sfid.size_bytes());

                mFiles.update_fn(fid, [&](vfilesystem_entry &e) {
                    if (!e.needs_index_update)
                    {
                        return;
                    }

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
        return mIndexTree->commit([
            this, maxExtent
        ](detail::root_sector_info rootInfo) noexcept->result<void> {
            return sync_commit_info(rootInfo, maxExtent);
        });
    }

    auto vfilesystem::sync_commit_info(detail::root_sector_info rootInfo,
                                       std::uint64_t maxExtent) noexcept
        -> result<void>
    {
        rootInfo.maximum_extent = maxExtent;

        mDevice.archive_header().filesystem_index.tree_info = rootInfo;
        VEFS_TRY_INJECT(mDevice.update_header(),
                        ed::archive_file{"[archive-header]"});

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

        // precondition the central directory index is currently commited
        {
            VEFS_TRY(indexTree, inspection_tree::open_existing(
                                    mDevice, mInfo.crypto_ctx, mInfo.tree_info,
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
