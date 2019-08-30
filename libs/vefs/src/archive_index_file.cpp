#include "archive_index_file.hpp"
#include "precompiled.hpp"

#include <vefs/detail/tree_lut.hpp>
#include <vefs/utils/bitset_overlay.hpp>

#include "archive_file_lookup.hpp"
#include "block_manager.hpp"
#include "proto-helper.hpp"

namespace vefs
{
    archive::index_file::index_file(archive &owner)
        : file_events{}
        , archive::internal_file{owner, owner.mArchive->index_file(), *this}
        , mIndex{}
        , mIOSync{}
        , mFileHandles{}
        , mFreeBlocks{}
        , mDirtFlag{}
    {
    }
    auto archive::index_file::open(archive &owner) -> result<std::shared_ptr<archive::index_file>>
    {
        return internal_file::open<index_file>(owner);
    }
    auto archive::index_file::create_new(archive &owner)
        -> result<std::shared_ptr<archive::index_file>>
    {
        BOOST_OUTCOME_TRY(self, internal_file::create_new<index_file>(owner));

        BOOST_OUTCOME_TRY(self->resize(detail::raw_archive::sector_payload_size));
        self->mFreeBlocks.dealloc(0, blocks_per_sector);

        return std::move(self);
    }

    auto archive::index_file::open(const std::string_view filePath,
                                   const file_open_mode_bitset mode) -> result<file_handle>
    {
        using detail::file_id;

        file_id id;
        file_lookup_ptr lookup;

        const auto acquire_fn = [&lookup](const file_lookup_ptr &f) noexcept
        {
            lookup = f;
        };

        if (mIndex.find_fn(filePath, [&id](const file_id &elem) { id = elem; }))
        {
            if (mFileHandles.find_fn(id, acquire_fn))
            {
                if (auto lrx = lookup->load(mOwner))
                {
                    return std::move(lrx).assume_value();
                }
                else if (lrx.assume_error() == errc::not_enough_memory)
                {
                    return std::move(lrx).as_failure();
                }
                lookup = nullptr;
            }
        }
        if (mode % file_open_mode::create)
        {
            file_handle result;

            if (auto crx = file_lookup::create(mOwner, std::string{filePath}))
            {
                std::tie(lookup, result) = std::move(crx).assume_value();
                id = lookup->meta_data().id;
            }
            else
            {
                return std::move(crx).as_failure();
            }

            if (!mFileHandles.insert(id, lookup))
            {
                // retry
                return open(filePath, mode);
            }

            if (!mIndex.insert(filePath, id))
            {
                // rollback, someone was faster
                result = nullptr;
                // ignore the error
                (void)lookup->try_kill(mOwner);
                mFileHandles.erase(id);

                // retry
                return open(filePath, mode);
            }
            mDirtFlag.mark();

            return result;
        }

        // #TODO refine open failure exception
        return archive_errc::no_such_file;
    }

    auto archive::index_file::erase(std::string_view filePath) -> result<void>
    {
        using detail::file_id;

        file_id fid;
        if (!mIndex.find_fn(filePath, [&fid](const file_id &elem) { fid = elem; }))
        {
            return archive_errc::no_such_file;
        }

        file_lookup_ptr lookup;
        mFileHandles.find_fn(fid, [&lookup](const file_lookup_ptr &l) { lookup = l; });
        if (lookup)
        {
            BOOST_OUTCOME_TRY(lookup->try_kill(mOwner));
            mIndex.erase(filePath);
            mFileHandles.erase(fid);

            if (lookup->mIndexBlockPosition != -1)
            {
                dealloc_blocks(lookup->mIndexBlockPosition, lookup->mReservedIndexBlocks);
                auto treePos = treepos_of(lookup->mIndexBlockPosition);
                auto endPos =
                    treepos_of(lookup->mIndexBlockPosition + lookup->mReservedIndexBlocks - 1);
                sector::handle hSector;
                do
                {
                    BOOST_OUTCOME_TRY(hSector, access(treePos));
                    write_block_header(std::move(hSector));

                    treePos.position(treePos.position() + 1);
                } while (treePos.position() <= endPos.position());
            }
        }
        mDirtFlag.mark();

        return outcome::success();
    }

    auto archive::index_file::query(const std::string_view filePath) -> result<file_query_result>
    {
        using detail::file_id;

        file_id id;
        if (mIndex.find_fn(filePath, [&id](const detail::file_id &elem) { id = elem; }))
        {
            file_query_result result;
            if (mFileHandles.find_fn(id, [&result](const auto &l) {
                    const auto &meta = l->meta_data();
                    result.size = meta.size;
                }))
            {
                result.allowed_flags = file_open_mode::readwrite | file_open_mode::truncate;
                return result;
            }
        }
        return archive_errc::no_such_file;
    }

    auto archive::index_file::sync(bool full) -> result<bool>
    {
        std::lock_guard ioLock{mIOSync};
        auto lockedIndex = mIndex.lock_table();

        std::vector<std::byte> serializationBufferMem;
        serializationBufferMem.reserve(16 * block_size);
        adesso::vefs::FileDescriptor descriptor;

        for (const auto &[path, fid] : lockedIndex)
        {
            auto lookup = mFileHandles.find(fid);
            auto fileHandle = lookup->try_load();
            if (full && fileHandle)
            {
                (void)deref(fileHandle.assume_value())->sync();
            }

            if (!lookup->mDirtyMetaData.is_dirty())
            {
                continue;
            }

            {
                std::unique_lock<std::shared_mutex> metalock;
                if (fileHandle)
                {
                    metalock = deref(fileHandle.assume_value())->lock_integrity();
                }

                pack(descriptor, lookup->mMeta);
            }
            descriptor.set_filepath(path);

            const auto size = descriptor.ByteSizeLong();
            const auto neededBlocks = utils::div_ceil(size + 2, block_size);
            assert(neededBlocks <= static_cast<size_t>(std::numeric_limits<int>::max()));
            const auto sNeededBlocks = static_cast<int>(neededBlocks);

            if (lookup->mReservedIndexBlocks < sNeededBlocks)
            {
                if (lookup->mIndexBlockPosition != -1)
                {
                    // first we try to extend our existing allocation
                    auto gap = neededBlocks - lookup->mReservedIndexBlocks;
                    auto r = mFreeBlocks.try_extend(
                        lookup->mIndexBlockPosition,
                        lookup->mIndexBlockPosition + lookup->mReservedIndexBlocks - 1, gap);

                    if (r != 0)
                    {
                        // extension was successful
                        // r = -1 if blocks were reserved before block pos
                        // r = 1 if blocks were reserved after the end of current block
                        if (r < 0)
                        {
                            lookup->mIndexBlockPosition -= static_cast<int>(gap);
                        }
                        lookup->mReservedIndexBlocks = static_cast<int>(neededBlocks);
                    }
                    else
                    {
                        dealloc_blocks(lookup->mIndexBlockPosition, lookup->mReservedIndexBlocks);
                        lookup->mIndexBlockPosition = -1;
                        lookup->mReservedIndexBlocks = 0;
                    }
                }

                if (lookup->mIndexBlockPosition == -1)
                {
                    // no existing allocation could be used

                    auto newPos = mFreeBlocks.alloc_consecutive(neededBlocks);
                    while (!newPos.has_value())
                    {
                        // grow one sector
                        auto oldFileSize = this->size();
                        BOOST_OUTCOME_TRY(
                            resize(oldFileSize + detail::raw_archive::sector_payload_size));
                        mFreeBlocks.dealloc(detail::lut::sector_position_of(oldFileSize) /
                                                blocks_per_sector,
                                            blocks_per_sector);

                        newPos = mFreeBlocks.alloc_consecutive(neededBlocks);
                    }

                    lookup->mIndexBlockPosition = static_cast<int>(newPos.value());
                    lookup->mReservedIndexBlocks = static_cast<int>(neededBlocks);
                }
            }

            serializationBufferMem.resize(size + sizeof(std::uint16_t));
            span serializationBuffer{serializationBufferMem};
            {
                const auto xsize = static_cast<std::uint16_t>(size);
                copy(ro_blob_cast(xsize), serializationBuffer);
            }

            serialize_to_blob(serializationBuffer.subspan(sizeof(std::uint16_t)), descriptor);

            BOOST_OUTCOME_TRY(write_blocks(lookup->mIndexBlockPosition, serializationBuffer, true));

            lookup->mDirtyMetaData.unmark();
        }

        BOOST_OUTCOME_TRY(internal_file::sync());

        return mDirtFlag.is_dirty();
    }

    auto archive::index_file::sync_open_files() -> result<bool>
    {
        for (auto &f : mFileHandles.lock_table())
        {
            if (auto fh = f.second->try_load())
            {
                BOOST_OUTCOME_TRY(file_lookup::deref(fh.assume_value())->sync());
            }
        }

        return mDirtFlag.is_dirty();
    }

    void archive::index_file::notify_meta_update([[maybe_unused]] file_lookup_ptr lookup,
                                                 [[maybe_unused]] file *ws)
    {

        // const auto sectorPos = treepos_of(lookup->mIndexBlockPosition);
        // auto sector = try_access(sectorPos);
        // if (!sector)
        //{
        //    return;
        //}

        // adesso::vefs::FileDescriptor descriptor;
    }

    void archive::index_file::on_sector_write_suggestion([[maybe_unused]] sector_handle sector)
    {
        on_dirty_sector(std::move(sector));
    }

    void archive::index_file::on_root_sector_synced([
        [maybe_unused]] detail::basic_archive_file_meta &rootMeta)
    {
        mDirtFlag.mark();
    }

    void archive::index_file::on_sector_synced([[maybe_unused]] detail::sector_id physId,
                                               [[maybe_unused]] ro_blob<16> mac)
    {
        mDirtFlag.mark();
    }

    auto archive::index_file::parse_content() -> result<void>
    {
        std::lock_guard<std::mutex> ioLock{mIOSync};

        tree_position it{0};
        adesso::vefs::FileDescriptor descriptor;

        const auto fileSize = size();

        for (std::uint64_t consumed = 0; consumed < fileSize;
             consumed += detail::raw_archive::sector_payload_size)
        {
            file::sector::handle sector;
            if (auto arx = access(it))
            {
                sector = std::move(arx).assume_value();
            }
            else
            {
                // #TODO error reporting
                // #TODO damage mitigation
                continue;
            }

            auto xSectorBlob = sector->data_view();
            auto allocMapBlob = xSectorBlob.first<alloc_map_size>();
            auto sectorBlob = xSectorBlob.subspan(
                alloc_map_size, xSectorBlob.extent - alloc_map_size - sector_padding);

            const auto blockIdxOffset = it.position() * blocks_per_sector;
            utils::const_bitset_overlay allocMap{allocMapBlob};

            for (auto i = 0u; i < blocks_per_sector;)
            {
                const auto startBlock = static_cast<int>(blockIdxOffset + i);

                if (!allocMap[i])
                {
                    mFreeBlocks.dealloc(startBlock);
                    ++i;
                    sectorBlob = sectorBlob.subspan(block_size);
                    continue;
                }

                std::uint16_t descriptorLength;
                copy(sectorBlob, rw_blob_cast(descriptorLength));

                const auto numBlocks = utils::div_ceil(descriptorLength, block_size);
                if (numBlocks + i >= blocks_per_sector)
                {
                    // #TODO #FIXME correctly parse an index entry spanning
                    // two sectors (which will be produced by our impl)
                    return archive_errc::index_entry_spanning_blocks;
                }
                for (const unsigned j = i; i - j < numBlocks; ++i)
                {
                    if (!allocMap[i])
                    {
                        return archive_errc::corrupt_index_entry;
                    }
                }

                detail::basic_archive_file_meta currentFile{};
                {
                    if (!parse_blob(descriptor, sectorBlob.subspan(2, descriptorLength)))
                    {
                        return error{archive_errc::corrupt_index_entry}
                               << ed::wrapped_error{archive_errc::invalid_proto};
                    }
                    VEFS_SCOPE_EXIT
                    {
                        erase_secrets(descriptor);
                    };

                    unpack(currentFile, descriptor);
                }

                auto currentId = currentFile.id;
                auto lookup = file_lookup::open(std::move(currentFile), descriptor.filepath(),
                                                startBlock, numBlocks)
                                  .value();

                mIndex.insert_or_assign(descriptor.filepath(), currentId);
                mFileHandles.insert(currentId, std::move(lookup));

                sectorBlob = sectorBlob.subspan(numBlocks * block_size);
            }

            it.position(it.position() + 1);
        }

        return outcome::success();
    }

    void archive::index_file::dealloc_blocks(int first, int num)
    {
        mFreeBlocks.dealloc(static_cast<std::uint64_t>(first), static_cast<std::uint64_t>(num));

        while (num > 0)
        {
            sector_handle sector;
            if (auto arx = access(treepos_of(first)))
            {
                sector = arx.assume_value();
            }
            else
            {
                // #TODO error reporting
                // #TODO damage mitigation
                continue;
            }
            std::lock_guard<std::shared_mutex> sectorLock{sector->data_sync()};
            sector.mark_dirty();

            write_block_header(sector);

            num -= blocks_per_sector;
            first += blocks_per_sector;
        }
    }

    auto archive::index_file::write_blocks(int indexBlockPos, ro_dynblob data, bool updateAllocMap)
        -> result<void>
    {
        auto remaining = write_blocks_impl(indexBlockPos, data, updateAllocMap);
        while (remaining)
        {
            auto [nextIndex, remainingData] = remaining.assume_value();
            remaining = write_blocks_impl(nextIndex, remainingData, updateAllocMap);
        }
        if (remaining.assume_error() != errc::no_more_data)
        {
            return std::move(remaining).as_failure();
        }
        return outcome::success();
    }

    auto archive::index_file::write_blocks_impl(int indexBlockPos, ro_dynblob data,
                                                bool updateAllocMap)
        -> result<std::tuple<int, ro_dynblob>>
    {
        const auto treePos = treepos_of(indexBlockPos);
        BOOST_OUTCOME_TRY(hSector, access(treePos));

        auto localBlockPos = indexBlockPos % blocks_per_sector;
        auto writePos = alloc_map_size + static_cast<std::uint64_t>(localBlockPos) * block_size;
        auto maxWriteBlocks = blocks_per_sector - localBlockPos;

        std::lock_guard sectorLock{hSector->data_sync()};
        hSector.mark_dirty();

        const auto portion = std::min(data.size(), maxWriteBlocks * block_size);
        copy(data.first(portion), hSector->data().subspan(writePos));
        if (updateAllocMap)
        {
            write_block_header(hSector);
        }

        if (maxWriteBlocks * block_size != portion)
        {
            return errc::no_more_data;
        }
        else
        {
            int nextPos = static_cast<int>(indexBlockPos + maxWriteBlocks);
            return std::tuple{nextPos, data.subspan(maxWriteBlocks * block_size)};
        }
    }

    void archive::index_file::write_block_header(sector_handle handle)
    {
        assert(handle);

        std::array<std::byte, alloc_map_size> serializedDataStorage;
        span serializedData{serializedDataStorage};
        utils::bitset_overlay allocMap{serializedData};

        // range[begin, end] to be written
        const auto begin = handle->position().position() * blocks_per_sector;

        serializedData.back() = std::byte{0}; // force the last two (unused) bits to zero
        mFreeBlocks.write_to_bitset(allocMap, begin, blocks_per_sector);

        copy(serializedData, handle->data());
    }
} // namespace vefs
