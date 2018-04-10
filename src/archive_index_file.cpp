#include "precompiled.hpp"
#include "archive_index_file.hpp"

#include <vefs/utils/bitset_overlay.hpp>
#include <vefs/detail/tree_lut.hpp>

#include "archive_file_lookup.hpp"
#include "block_manager.hpp"
#include "proto-helper.hpp"

namespace vefs
{
    archive::index_file::index_file(archive &owner, detail::basic_archive_file_meta &meta)
        : file_events{}
        , archive::internal_file{ owner, meta, *this }
    {
        parse_content();
    }
    archive::index_file::index_file(archive &owner, detail::basic_archive_file_meta &meta,
        create_tag)
        : file_events{}
        , archive::internal_file{ owner, meta, *this, archive::create }
    {
        resize(detail::raw_archive::sector_payload_size);
        mFreeBlocks.dealloc(0, blocks_per_sector);
    }

     auto archive::index_file::open(const std::string_view filePath,
        const file_open_mode_bitset mode)
        -> file_handle
    {
        using detail::file_id;

        file_id id;
        file_lookup_ptr lookup;
        file_handle result;

        auto acquire_fn = [&lookup](const file_lookup_ptr &f)
        {
            lookup = f;
        };

        if (mIndex.find_fn(filePath, [&id](const file_id &elem) { id = elem; }))
        {
            if (mFileHandles.find_fn(id, acquire_fn))
            {
                if (result = lookup->load(mOwner))
                {
                    return result;
                }
                lookup = nullptr;
            }
        }
        if (mode % file_open_mode::create)
        {
            {
                auto file = mOwner.mArchive->create_file();
                id = file->id;

                // file will be moved from, so after this line file contains garbage
                lookup = utils::make_ref_counted<file_lookup>(*file, mOwner,
                    result, file_lookup::create);
            }

            if (!mFileHandles.insert(id, lookup))
            {
                BOOST_THROW_EXCEPTION(logic_error{});
            }

            if (!mIndex.insert(filePath, id))
            {
                // rollback, someone was faster
                result = nullptr;
                lookup->try_kill(mOwner);
                mFileHandles.erase(id);

                return open(filePath, mode);
            }
            mDirtFlag.mark();

            return result;
        }

        // #TODO refine open failure exception
        BOOST_THROW_EXCEPTION(file_not_found{});
    }

    void archive::index_file::erase(std::string_view filePath)
    {
        using detail::file_id;

        file_id fid;
        if (!mIndex.find_fn(filePath, [&fid](const file_id &elem) { fid = elem; }))
        {
            BOOST_THROW_EXCEPTION(file_not_found{});
        }

        file_lookup_ptr lookup;
        mFileHandles.find_fn(fid, [&lookup](const file_lookup_ptr &l)
        {
            lookup = l;
        });
        if (lookup)
        {
            if (!lookup->try_kill(mOwner))
            {
                BOOST_THROW_EXCEPTION(file_still_open{});
            }
            mIndex.erase(filePath);
            mFileHandles.erase(fid);
        }
        mDirtFlag.mark();
    }

    auto archive::index_file::query(const std::string_view filePath)
        -> std::optional<file_query_result>
    {
        using detail::file_id;

        file_id id;
        if (mIndex.find_fn(filePath, [&id](const detail::file_id &elem) { id = elem; }))
        {
            file_query_result result;
            if (mFileHandles.find_fn(id, [&result](const auto &l)
            {
                const auto &meta = l->meta_data();
                result.size = meta.size;
            }))
            {
                result.allowed_flags
                    = file_open_mode::readwrite | file_open_mode::truncate;
                return result;
            }
        }
        return std::nullopt;
    }

    auto archive::index_file::sync(bool full) -> bool
    {
        std::lock_guard<std::mutex> ioLock{ mIOSync };
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
                file_lookup::deref(fileHandle)->sync();
            }

            if (!lookup->mDirtyMetaData.is_dirty())
            {
                continue;
            }

            {
                std::unique_lock<std::shared_mutex> metalock;
                if (fileHandle)
                {
                    metalock = file_lookup::deref(fileHandle)->lock_integrity();
                }

                pack(descriptor, lookup->mMeta);
            }
            descriptor.set_filepath(path);

            const auto size = descriptor.ByteSizeLong();
            const auto neededBlocks = utils::div_ceil(size + 2, block_size);
            assert(neededBlocks <= static_cast<size_t>(std::numeric_limits<int>::max()));
            const auto sNeededBlocks = static_cast<int>(neededBlocks);

            sector_handle sector;
            if (lookup->mReservedIndexBlocks < sNeededBlocks)
            {
                if (lookup->mIndexBlockPosition != -1)
                {
                    // first we try to extend our existing allocation
                    auto gap = neededBlocks - lookup->mReservedIndexBlocks;
                    auto r = mFreeBlocks.try_extend(lookup->mIndexBlockPosition,
                        lookup->mIndexBlockPosition + lookup->mReservedIndexBlocks - 1,
                        gap);

                    if (r != 0)
                    {
                        // extension was successful
                        // r = -1 if blocks were reserved before block pos
                        // r = 1 if blocks were reserved after the end of current block
                        lookup->mIndexBlockPosition += static_cast<int>(r * gap);
                        lookup->mReservedIndexBlocks = static_cast<int>(neededBlocks);
                    }
                    else
                    {
                        dealloc_blocks(lookup->mIndexBlockPosition,
                            lookup->mReservedIndexBlocks);
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
                        resize(oldFileSize + detail::raw_archive::sector_payload_size);
                        mFreeBlocks.dealloc(
                            detail::lut::sector_position_of(oldFileSize) / blocks_per_sector,
                            blocks_per_sector);

                        newPos = mFreeBlocks.alloc_consecutive(neededBlocks);
                    }

                    lookup->mIndexBlockPosition = static_cast<int>(newPos.value());
                    lookup->mReservedIndexBlocks = static_cast<int>(neededBlocks);
                }
            }

            serializationBufferMem.resize(size + sizeof(std::uint16_t));
            blob serializationBuffer{ serializationBufferMem };
            serializationBuffer.as<std::uint16_t>() = static_cast<std::uint16_t>(size);

            serialize_to_blob(serializationBuffer.slice(sizeof(std::uint16_t)), descriptor);

            write_blocks(lookup->mIndexBlockPosition, serializationBuffer, true);

            lookup->mDirtyMetaData.unmark();
        }

        internal_file::sync();

        return mDirtFlag.is_dirty();
    }

    auto archive::index_file::sync_open_files() -> bool
    {
        for (auto &f : mFileHandles.lock_table())
        {
            if (auto fh = f.second->try_load())
            {
                file_lookup::deref(fh)->sync();
            }
        }

        return mDirtFlag.is_dirty();
    }

    void archive::index_file::notify_meta_update([[maybe_unused]] file_lookup_ptr lookup,
        [[maybe_unused]] file *ws)
    {

        //const auto sectorPos = treepos_of(lookup->mIndexBlockPosition);
        //auto sector = try_access(sectorPos);
        //if (!sector)
        //{
        //    return;
        //}

        //adesso::vefs::FileDescriptor descriptor;
    }

    void archive::index_file::on_sector_write_suggestion([[maybe_unused]] sector_handle sector)
    {
        on_dirty_sector(std::move(sector));
    }

    void archive::index_file::on_root_sector_synced(
        [[maybe_unused]] detail::basic_archive_file_meta &rootMeta)
    {
        mDirtFlag.mark();
    }

    void archive::index_file::on_sector_synced([[maybe_unused]] detail::sector_id physId,
        [[maybe_unused]] blob_view mac)
    {
        mDirtFlag.mark();
    }

    void archive::index_file::parse_content()
    {
        std::lock_guard<std::mutex> ioLock{ mIOSync };

        tree_position it{ 0 };
        adesso::vefs::FileDescriptor descriptor;

        const auto fileSize = size();

        for (std::uint64_t consumed = 0;
            consumed < fileSize;
            consumed += detail::raw_archive::sector_payload_size)
        {
            auto sector = access(it);

            auto sectorBlob = sector->data();
            auto allocMapBlob = sectorBlob.slice(0, 64);
            sectorBlob.remove_prefix(alloc_map_size);
            sectorBlob.remove_suffix(sector_padding);

            const auto blockIdxOffset = it.position() * blocks_per_sector;
            utils::bitset_overlay allocMap{ allocMapBlob };

            for (auto i = 0; i < blocks_per_sector; )
            {
                const auto startBlock = static_cast<int>(blockIdxOffset + i);

                if (!allocMap[i])
                {
                    mFreeBlocks.dealloc(startBlock);
                    ++i;
                    sectorBlob.remove_prefix(block_size);
                    continue;
                }

                const auto descriptorLength = sectorBlob.as<std::uint16_t>();
                const auto numBlocks = utils::div_ceil(descriptorLength, block_size);
                if (numBlocks + i >= blocks_per_sector)
                {
                    BOOST_THROW_EXCEPTION(archive_corrupted{}
                        << errinfo_code{ archive_error_code::corrupt_index_entry }
                    );
                }
                for (const unsigned j = i; i - j < numBlocks; ++i)
                {
                    if (!allocMap[i])
                    {
                        BOOST_THROW_EXCEPTION(archive_corrupted{}
                            << errinfo_code{ archive_error_code::corrupt_index_entry }
                        );
                    }
                }

                auto currentFile = std::make_unique<detail::basic_archive_file_meta>();
                {
                    if (!parse_blob(descriptor, sectorBlob.slice(2, descriptorLength)))
                    {
                        BOOST_THROW_EXCEPTION(archive_corrupted{}
                            << errinfo_code{ archive_error_code::corrupt_index_entry }
                        );
                    }
                    VEFS_SCOPE_EXIT{ erase_secrets(descriptor); };

                    unpack(*currentFile, descriptor);
                }

                auto currentId = currentFile->id;
                mIndex.insert_or_assign(descriptor.filepath(), currentId);
                mFileHandles.insert(currentId,
                    utils::make_ref_counted<file_lookup>(*currentFile, startBlock, numBlocks));

                sectorBlob.remove_prefix(numBlocks * block_size);
            }

            it.position(it.position() + 1);
        }
    }

    void archive::index_file::dealloc_blocks(int first, int num)
    {
        mFreeBlocks.dealloc(static_cast<std::uint64_t>(first), static_cast<std::uint64_t>(num));

        while (num > 0)
        {
            auto sector = access(treepos_of(first));
            std::lock_guard<std::shared_mutex> sectorLock{ sector->data_sync() };
            sector.mark_dirty();

            write_block_header(sector);

            num -= blocks_per_sector;
            first += blocks_per_sector;
        }
    }

    void archive::index_file::write_blocks(int indexBlockPos, blob_view data, bool updateAllocMap)
    {
        auto remaining = write_blocks_impl(indexBlockPos, data, updateAllocMap);
        while (remaining)
        {
            remaining = write_blocks_impl(std::get<0>(*remaining), std::get<1>(*remaining),
                updateAllocMap);
        }
    }

    auto archive::index_file::write_blocks_impl(int indexBlockPos, blob_view data, bool updateAllocMap)
        -> std::optional<std::tuple<int, blob_view>>
    {
        const auto treePos = treepos_of(indexBlockPos);
        sector_handle sector = access(treePos);

        auto localBlockPos = indexBlockPos % blocks_per_sector;
        auto writePos = alloc_map_size + static_cast<std::uint64_t>(localBlockPos) * block_size;
        auto maxWriteBlocks = blocks_per_sector - localBlockPos;

        std::lock_guard<std::shared_mutex> sectorLock{ sector->data_sync() };
        sector.mark_dirty();

        data.slice(0, maxWriteBlocks * block_size).copy_to(sector->data().slice(writePos));
        if (updateAllocMap)
        {
            write_block_header(sector);
        }

        if (maxWriteBlocks * block_size > data.size())
        {
            return std::nullopt;
        }
        else
        {
            int nextPos = static_cast<int>(indexBlockPos + maxWriteBlocks);
            return { { nextPos, data.slice(maxWriteBlocks * block_size) } };
        }
    }

    void archive::index_file::write_block_header(sector_handle handle)
    {
        assert(handle);

        std::array<std::byte, alloc_map_size> serializedDataStorage;
        blob serializedData{ serializedDataStorage };
        utils::bitset_overlay allocMap{ serializedData };

        // range[begin, end] to be written
        const auto begin = handle->position().position() * blocks_per_sector;

        serializedData.back() = std::byte{0};
        mFreeBlocks.write_to_bitset(allocMap, begin, blocks_per_sector);

        constexpr auto limit =
            alloc_map_size * std::numeric_limits<std::underlying_type_t<std::byte>>::digits;

        serializedData.copy_to(handle->data());
    }
}
