#include "precompiled.hpp"
#include "archive_file.hpp"

#include <cassert>

#include <boost/range/adaptor/reversed.hpp>

#include "archive_free_block_list_file.hpp"

namespace vefs
{
    namespace
    {
#pragma pack(push, 1)

        struct RawSectorReference
        {
            detail::sector_id reference;
            std::array<std::byte, 8> VEFS_ANONYMOUS_VAR(_padding);
            std::array<std::byte, 16> mac;
        };
        static_assert(sizeof(RawSectorReference) == 32);

        struct RawFreeSectorRange
        {
            detail::sector_id start_sector;
            std::uint64_t num_sectors;
        };
        static_assert(sizeof(RawFreeSectorRange) == 16);

#pragma pack(pop)

        inline RawSectorReference & sector_reference_at(archive::file::sector &sector,
            int which)
        {
            return *(&(sector.data().as<RawSectorReference>()) + which);
        }
        inline const RawSectorReference & sector_reference_at(const archive::file::sector &sector,
            int which)
        {
            return *(&(sector.data().as<RawSectorReference>()) + which);
        }
        inline const RawSectorReference sector_creference_at(const archive::file::sector &sector,
            int which)
        {
            return sector_reference_at(sector, which);
        }

        inline bool is_allocated(std::uint64_t fileSize, detail::tree_position position)
        {
            const auto l = position.layer();
            const auto pos = position.position();
            // width of the referenced layer
            const auto unit_width = detail::lut::step_width[l];
            // step width on the reference layer
            const auto step_width = detail::lut::step_width[l + 1];
            const auto beginPos = pos * step_width;

            return ((pos | l) == 0) // there is always a sector allocated for each file
                || unit_width < fileSize && beginPos < fileSize;
        }
    }

    archive::file::file(archive & owner, detail::basic_archive_file_meta & data,
        file_events &hooks)
        : mOwner{ owner }
        , mHooks{ hooks }
        , mData{ data }
        , mCachedBlocks{}
        , mWriteFlag{}
    {
        mCachedBlocks = std::make_unique<block_pool_t>([&hooks](auto sector)
        {
            hooks.on_sector_write_suggestion(std::move(sector));
        });
    }

    archive::file::file(archive & owner, detail::basic_archive_file_meta & data,
        file_events &hooks, create_tag)
        : file{ owner, data, hooks }
    {
        assert(mData.tree_depth == -1);
        access_or_append(detail::tree_position{ 0, 0 });
    }

    archive::file::~file()
    {
    }

    std::optional<archive::file::sector::handle> archive::file::access_impl(
        tree_position sectorPosition)
    {
        using namespace vefs::detail;

        if (auto sector = mCachedBlocks->try_access(sectorPosition))
        {
            return sector;
        }

        std::uint64_t fileSize;
        int treeDepth;
        sector_id physId;
        decltype(mData.start_block_mac) mac;
        {
            std::shared_lock<std::shared_mutex> fileLock{ integrity_mutex };
            treeDepth = mData.tree_depth;
            physId = mData.start_block_idx;
            mac = mData.start_block_mac;
            fileSize = mData.size;
        }
        if (treeDepth < sectorPosition.layer() || !is_allocated(fileSize, sectorPosition))
        {
            return std::make_optional<file::sector::handle>();
        }

        tree_path path{ treeDepth, sectorPosition };
        auto pathIterator = path.cbegin();
        tree_path::iterator pathEnd{ path, sectorPosition.layer() };

        file::sector::handle parentSector;


        for (;;)
        {
            file::sector::handle sector;
            try
            {
                if (static_cast<uint64_t>(physId) >= mOwner.mArchive->size())
                {
                    BOOST_THROW_EXCEPTION(archive_corrupted{}
                        << errinfo_sector_idx{ physId }
                        << errinfo_code{ archive_error_code::sector_reference_out_of_range }
                    );
                }
                auto currentPosition = *pathIterator;
                auto entry = mCachedBlocks->access(currentPosition,
                    *this, parentSector, currentPosition, physId, blob_view{ mac });
                sector = std::move(std::get<1>(entry));
            }
            catch (const boost::exception &)
            {
                std::shared_lock<std::shared_mutex> fileLock{ integrity_mutex };
                // if the file tree shrinks during an access operation it may happen
                // that one of the intermediate nodes dies :(
                // however this is detectable and recoverable if the cut off part
                // doesn't contain the block we want to access
                const auto nTreeDepth = mData.tree_depth;
                if (nTreeDepth < sectorPosition.layer())
                {
                    return std::make_optional<file::sector::handle>();
                }
                if (treeDepth > nTreeDepth)
                {
                    return std::nullopt;
                }

                // it can happen that the target sector is written to disk and and removed from the
                // cache after we obtained its mac in the last iteration, if we detect such case
                // we just reread the mac and (the maybe updated sector idx) and try again with them
                if (parentSector)
                {
                    fileLock.unlock();
                    // stabilize memory representation
                    std::lock_guard<std::shared_mutex> parentLock{ parentSector->data_sync() };

                    auto &ref = sector_creference_at(*parentSector,
                        path.offset(sectorPosition.layer()));
                    if (ref.reference == sector_id::master)
                    {
                        return std::make_optional<file::sector::handle>();
                    }
                    if (!equal(blob_view{ mac }, blob_view{ ref.mac }))
                    {
                        physId = ref.reference;
                        mac = ref.mac;
                        continue;
                    }
                }
                else if (!equal(blob_view{ mac }, blob_view{ mData.start_block_mac }))
                {
                    return std::nullopt;
                }
                // #TODO add additional information like failed file_sector_id
                throw;
            }


            if (pathIterator == pathEnd)
            {
                return sector;
            }

            auto &ref = sector_creference_at(*sector, (++pathIterator).array_offset());

            physId = ref.reference;
            mac = ref.mac;
            // this will happen if we read past the end of the file
            if (physId == sector_id::master)
            {
                return std::make_optional<file::sector::handle>();
            }

            parentSector = std::move(sector);
        }
    }
    archive::file::sector::handle archive::file::access(tree_position sectorPosition)
    {
        if (!sectorPosition)
        {
            BOOST_THROW_EXCEPTION(invalid_argument{}
                << errinfo_param_name{ "sectorPosition" }
                << errinfo_param_misuse_description{ "the given sector position isn't valid" }
            );
        }
        std::optional<file::sector::handle> result;
        do
        {
            try
            {
                result = access_impl(sectorPosition);
            }
            catch (const boost::exception &)
            {
                //TODO: add additional information like requested file_sector_id
                throw;
            }
        } while (!result);
        return *result;
    }

    void archive::file::read(blob buffer, std::uint64_t readPos)
    {
        auto offset = readPos % detail::raw_archive::sector_payload_size;
        tree_position it{ detail::lut::sector_position_of(readPos) };

        while (buffer)
        {
            auto sector = access(it);
            it.position(it.position() + 1);
            if (!sector)
            {
                BOOST_THROW_EXCEPTION(invalid_argument{}
                    << errinfo_param_misuse_description{ "tried to read after the end of an archive file" }
                );
            }

            auto chunk = sector->data_view().slice(offset);
            offset = 0;

            std::shared_lock<std::shared_mutex> guard{ sector->data_sync() };
            chunk.copy_to(buffer);
            buffer.remove_prefix(chunk.size());
        }
    }

    archive::file::sector::handle archive::file::access_or_append(const tree_position position)
    {
        using namespace detail;

        assert(position.layer() == 0);

        if (auto sector = mCachedBlocks->try_access(position))
        {
            return sector;
        }

        auto requiredDepth = lut::required_tree_depth(position.position());
        tree_path path{ requiredDepth, position };

        std::shared_lock<std::shared_mutex> fileReadLock{ integrity_mutex };
        const tree_position rootPos{ 0, std::max(mData.tree_depth, requiredDepth) };

        // check whether we need to increase the tree depth
        file::sector::handle parent;
        if (requiredDepth > mData.tree_depth)
        {
            fileReadLock.unlock();
            std::unique_lock<std::shared_mutex> fileWriteLock{ integrity_mutex };

            if (requiredDepth > mData.tree_depth)
            {
                auto physId = mOwner.mFreeBlockIndexFile->alloc_sector();
                auto entry = mCachedBlocks->access(rootPos, parent, rootPos, physId);

                if (std::get<0>(entry))
                {
                    // we got a cached sector entry, i.e. the previously
                    // allocated physical sector needs to be freed.
                    mOwner.mFreeBlockIndexFile->dealloc_sectors({ physId });
                    // this shouldn't ever happen though, therefore trigger the debugger
                    assert(!std::get<0>(entry));
                }

                parent = std::move(std::get<1>(entry));

                auto &ref = sector_reference_at(*parent, 0);
                ref.reference = mData.start_block_idx;
                ref.mac = mData.start_block_mac;

                mData.start_block_idx = physId;
                mData.start_block_mac = {};
                mData.tree_depth += 1;

                parent.mark_dirty();

                // update the old root if it is currently cached
                const tree_position oldRootPos{ 0, requiredDepth - 1 };
                if (auto oldRoot = mCachedBlocks->try_access(oldRootPos))
                {
                    oldRoot->update_parent(parent);
                }
            }
        }
        else if (is_allocated(mData.size, position))
        {
            fileReadLock.unlock();
            return access(position);
        }
        else
        {
            fileReadLock.unlock();
        }

        if (!parent)
        {
            // tree depth wasn't increased
            // => try to find a cached intermediate node
            //    otherwise load the root node
            // note that this is not an `else if` because it also needs to
            // be executed if the second depth check is satisfied.

            for (auto tpos : boost::adaptors::reverse(path))
            {
                if (parent = mCachedBlocks->try_access(tpos))
                {
                    break;
                }
            }
            if (!parent)
            {
                parent = access(rootPos);
            }
        }

        // #TODO #resilience consider implementing tree allocation rollback.

        // walk the tree path down to layer 0 inserting missing sectors
        // if parent is on layer 0 then `it` is automatically an end iterator
        for (auto it = tree_path::iterator{ path, parent->position().layer() - 1 },
            end = path.cend();
            it != end; ++it)
        {
            auto &ref = sector_reference_at(*parent, it.array_offset());

            std::unique_lock<std::shared_mutex> parentLock{ parent->data_sync() };
            if (ref.reference != sector_id::master)
            {
                parentLock.unlock();
                parent = access(*it);
            }
            else
            {
                parentLock.unlock();

                auto physId = mOwner.mFreeBlockIndexFile->alloc_sector();
                auto[cached, entry] = mCachedBlocks->access(*it, parent, *it, physId);

                if (cached)
                {
                    mOwner.mFreeBlockIndexFile->dealloc_sectors({ physId });
                }
                else
                {
                    std::unique_lock<std::shared_mutex> entryLock{ entry->data_sync(),
                        std::defer_lock };
                    std::lock(parentLock, entryLock);

                    ref.reference = entry->sector_id();
                    ref.mac = {};
                    parent.mark_dirty();
                }
                parent = std::move(entry);
            }
        }

        parent.mark_dirty();
        mWriteFlag.mark();
        return parent;
    }

    void archive::file::write(blob_view data, std::uint64_t writeFilePos)
    {
        if (!data)
        {
            return;
        }

        tree_position writePos{ detail::lut::sector_position_of(writeFilePos) };
        auto offset = writeFilePos % detail::raw_archive::sector_payload_size;

        auto newMinSize = writeFilePos + data.size();

        std::shared_lock<std::shared_mutex> shrinkLock{ shrink_mutex };
        // make sure that the file is valid up until our write starting point
        grow_file(writeFilePos + 1);

        while (data)
        {
            auto sector = access_or_append(writePos);
            writePos.position(writePos.position() + 1);

            auto amountWritten = write(sector, data, offset);
            offset = 0;

            data.remove_prefix(amountWritten);

            auto newSize = std::min(
                writePos.position() * detail::raw_archive::sector_payload_size, newMinSize
            );

            std::lock_guard<std::shared_mutex> integrityLock{ integrity_mutex };
            mData.size = std::max(mData.size, newSize);
        }
    }

    std::uint64_t archive::file::write(sector::handle &sector, blob_view data, std::uint64_t offset)
    {
        auto chunk = sector->data().slice(offset, data.size());

        std::lock_guard<std::shared_mutex> sectorLock{ sector->data_sync() };

        data.copy_to(chunk);

        sector.mark_dirty();
        mWriteFlag.mark();

        return chunk.size();
    }

    std::uint64_t archive::file::write_no_lock(sector::handle & sector, blob_view data, std::uint64_t offset)
    {
        auto chunk = sector->data().slice(offset, data.size());

        data.copy_to(chunk);

        sector.mark_dirty();
        mWriteFlag.mark();

        return chunk.size();
    }

    void archive::file::write_sector_to_disk(sector::handle sector)
    {
        if (!sector)
        {
            return;
        }

        std::shared_lock<std::shared_mutex> shrinkLock{ shrink_mutex, std::defer_lock };
        std::unique_lock<std::shared_mutex> sectorLock{ sector->data_sync(), std::defer_lock };
        std::lock(shrinkLock, sectorLock);
        VEFS_ERROR_EXIT{ sector.mark_dirty(); };
        VEFS_SCOPE_EXIT{ sector->write_queued_flag().clear(std::memory_order_release); };

        if (!sector.is_dirty())
        {
            return;
        }

        /* this case is currently handled by file_lookup
        if (!mData.valid)
        {
            sector.mark_clean();
            return;
        }
        */
        assert(is_allocated(mData.size, sector->position()));

        std::array<std::byte, detail::raw_archive::sector_payload_size + 16> encryptionMem;
        blob ciphertext{ encryptionMem };
        blob mac = ciphertext.slice(0, 16);
        ciphertext.remove_prefix(16);

        mOwner.mArchive->write_sector(ciphertext, mac, mData,
            sector->sector_id(), sector->data_view());

        // update parent sector with the new information
        // retry loop
        for (;;)
        {
            auto parent = sector->parent();
            if (parent)
            {
                std::lock_guard<std::shared_mutex> parentLock{ parent->data_sync() };

                auto offset = sector->position().parent_array_offset();
                auto &ref = sector_reference_at(*parent, offset);
                ref.reference = sector->sector_id();
                mac.copy_to(blob{ ref.mac });
                parent.mark_dirty();

                mHooks.on_sector_synced(ref.reference, mac);
            }
            else
            {
                std::unique_lock<std::shared_mutex> fileIntegrityLock{
                    integrity_mutex
                };
                if (sector->parent())
                {
                    fileIntegrityLock.unlock();
                    // the file grew a new root node
                    continue;
                }

                assert(mData.tree_depth == sector->position().layer());

                mData.start_block_idx = sector->sector_id();
                mac.copy_to(mData.start_block_mac_blob());

                mHooks.on_root_sector_synced(mData);
            }
            break;
        }

        sector.mark_clean();
    }

    std::unique_lock<std::shared_mutex> archive::file::lock_integrity()
    {
        return std::unique_lock<std::shared_mutex>{ integrity_mutex };
    }

    void archive::file::resize(std::uint64_t size)
    {
        std::unique_lock<std::shared_mutex> shrinkLock{ shrink_mutex };
        std::uint64_t fileSize;
        {
            std::unique_lock<std::shared_mutex> integrityLock{ integrity_mutex };
            fileSize = mData.size;
        }

        if (fileSize < size)
        {
            shrinkLock.unlock();
            std::shared_lock<std::shared_mutex> growLock{ shrink_mutex };
            grow_file(size);
        }
        else if (fileSize > size)
        {
            shrink_file(size);
        }
    }

    void archive::file::sync()
    {
        atomic_thread_fence(std::memory_order_acquire);

        auto layer = 0;
        bool dirtyElements;
        do
        {
            dirtyElements = mCachedBlocks->for_dirty([this, layer](block_pool_t::handle sector)
            {
                if (sector->position().layer() == layer)
                {
                    write_sector_to_disk(std::move(sector));
                }
            });
            layer = (layer + 1) % (detail::lut::max_tree_depth + 1);
        } while (dirtyElements);
    }

    void archive::file::erase_self()
    {
        std::unique_lock<std::shared_mutex> shrinkLock{ shrink_mutex };
        shrink_file(0);

        std::unique_lock<std::shared_mutex> integrityLock{ integrity_mutex };
        mOwner.mArchive->erase_sector(mData, mData.start_block_idx);
        mOwner.mFreeBlockIndexFile->dealloc_sectors({ mData.start_block_idx });
        mData.tree_depth = -1;
        mData.start_block_idx = detail::sector_id::master;
        mData.start_block_mac = {};
    }

    void archive::file::grow_file(std::uint64_t size)
    {
        using detail::raw_archive;

        auto endSectorPos = size
            ? (size - 1) / raw_archive::sector_payload_size
            : 0;
        std::uint64_t fileSize;
        {
            std::lock_guard<std::shared_mutex> integrityLock{ integrity_mutex };
            fileSize = mData.size;
        }
        auto startSectorPos = fileSize
            ? (fileSize - 1) / raw_archive::sector_payload_size
            : 0;

        // the first sector is always allocated
        tree_position posIt{ startSectorPos + 1 };

        // the loop immediately terminates if the file is already big enough
        while (posIt.position() <= endSectorPos)
        {
            if (!is_allocated(fileSize, posIt))
            {
                access_or_append(posIt);

                auto newSize = std::min(
                    (posIt.position() + 1) * raw_archive::sector_payload_size, size
                );

                std::lock_guard<std::shared_mutex> integrityLock{ integrity_mutex };
                fileSize = mData.size = std::max(mData.size, newSize);
            }
            posIt.position(posIt.position() + 1);
        }

        {
            std::lock_guard<std::shared_mutex> integrityLock{ integrity_mutex };
            if (mData.size < size)
            {
                mData.size = size;
            }
        }
    }

    void archive::file::shrink_file(const std::uint64_t size)
    {
        using detail::raw_archive;
        using detail::tree_path;
        namespace lut = detail::lut;

        std::uint64_t fileSize;
        int treeDepth;
        {
            std::lock_guard<std::shared_mutex> integrityLock{ integrity_mutex };
            fileSize = mData.size;
            treeDepth = mData.tree_depth;
        }
        // we always keep the first sector alive
        if (fileSize <= raw_archive::sector_payload_size)
        {
            std::lock_guard<std::shared_mutex> integrityLock{ integrity_mutex };
            mData.size = size;
            return;
        }

        std::vector<detail::sector_id> collectedIds;
        tree_path walker{ treeDepth, lut::sector_position_of(fileSize - 1) };
        auto endPosition = size != 0 ? lut::sector_position_of(size) : 0;

        while (walker.position(0) > endPosition)
        {
            auto it = mCachedBlocks->try_access(walker.layer_position(0));
            detail::sector_id toBeCollected;

            if (it)
            {
                std::lock_guard<std::shared_mutex> writeLock{ it->data_sync() };

                toBeCollected = it->sector_id();
                it.mark_clean();

                auto tmp = it->parent();
                it->update_parent({});
                it = std::move(tmp);
            }
            else
            {
                it = access(walker.layer_position(1));

                auto offset = walker.offset(0);
                auto ref = sector_reference_at(*it, offset);
                toBeCollected = ref.reference;
            }

            collectedIds.push_back(toBeCollected);
            mOwner.mArchive->erase_sector(mData, toBeCollected);

            // update all parent sectors affected by the removal of the current sector
            for (auto layer = 1; ; ++layer)
            {
                std::lock_guard<std::shared_mutex> writeLock{ it->data_sync() };

                auto offset = walker.offset(layer - 1);
                auto ref = sector_reference_at(*it, offset);
                utils::secure_data_erase(ref);
                it.mark_dirty();

                auto tmp = it->parent();

                if (offset != 0)
                {
                    // isn't the first reference stored here, so we need to keep
                    // this sector for now.
                    // no more parents need to be updated
                    break;
                }
                else if (walker.position(layer) != 0)
                {
                    // this reference sector isn't needed anymore
                    // we don't immediately erase the sectors at the beginning
                    // of each layer as this would involve reducing the height
                    // of the tree which we'll do afterwards

                    auto sectorIdx = it->sector_id();
                    collectedIds.push_back(sectorIdx);
                    mOwner.mArchive->erase_sector(mData, sectorIdx);
                    it.mark_clean();

                    it->update_parent({});
                }

                it = std::move(tmp);
            }

            walker = walker.previous();
        }

        // now we only need to adjust the height of the file tree
        auto adjustedDepth = lut::required_tree_depth(endPosition);
        if (adjustedDepth != treeDepth)
        {
            auto it = access(tree_position{ 0, adjustedDepth });

            auto parent = it->parent();
            auto ref = &parent->data().as<RawSectorReference>();

            std::lock_guard<std::shared_mutex> integrityLock{ integrity_mutex };
            mData.start_block_idx = ref->reference;
            mData.start_block_mac = ref->mac;
            mData.tree_depth = adjustedDepth;
            mData.size = size;

            // loop over all parents and free them
            do
            {
                it->update_parent({});

                it = std::move(parent);

                utils::secure_data_erase(it->data().as<RawSectorReference>());
                auto sectorIdx = it->sector_id();
                collectedIds.push_back(sectorIdx);
                mOwner.mArchive->erase_sector(mData, sectorIdx);
                it.mark_clean();

                parent = it->parent();
            } while (parent);
        }
        else
        {
            std::lock_guard<std::shared_mutex> integrityLock{ integrity_mutex };
            mData.size = size;
        }

        mOwner.mFreeBlockIndexFile->dealloc_sectors(std::move(collectedIds));
    }
}

