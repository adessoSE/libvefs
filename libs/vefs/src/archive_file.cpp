#include "archive_file.hpp"
#include "precompiled.hpp"

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

        inline RawSectorReference &sector_reference_at(archive::file::sector &sector, int which)
        {
            // #UB-ObjectLifetime
            return *(reinterpret_cast<RawSectorReference *>(sector.data().data()) + which);
        }
        inline const RawSectorReference &sector_reference_at(const archive::file::sector &sector,
                                                             int which)
        {
            // #UB-ObjectLifetime
            return *(reinterpret_cast<const RawSectorReference *>(sector.data().data()) + which);
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
                   || (unit_width < fileSize && beginPos < fileSize);
        }
    } // namespace

    archive::file::file(archive &owner, detail::basic_archive_file_meta &data, file_events &hooks)
        : mOwner{owner}
        , mHooks{hooks}
        , mData{data}
        , mCachedBlocks{}
        , mWriteFlag{}
    {
        mCachedBlocks = std::make_unique<block_pool_t>(
            [&hooks](auto sector) { hooks.on_sector_write_suggestion(std::move(sector)); });
    }

    auto archive::file::create_self() -> result<void>
    {
        assert(mData.tree_depth == -1);
        BOOST_OUTCOME_TRY(access_or_append(detail::tree_position{0, 0}));
        return outcome::success();
    }

    archive::file::~file()
    {
    }

    auto archive::file::lock_integrity() -> std::unique_lock<std::shared_mutex>
    {
        return std::unique_lock{integrity_mutex};
    }

    auto archive::file::access_impl(tree_position sectorPosition) -> result<file::sector::handle>
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
            std::shared_lock fileLock{integrity_mutex};
            treeDepth = mData.tree_depth;
            physId = mData.start_block_idx;
            mac = mData.start_block_mac;
            fileSize = mData.size;
        }
        if (treeDepth < sectorPosition.layer() || !is_allocated(fileSize, sectorPosition))
        {
            return archive_errc::sector_reference_out_of_range;
        }

        tree_path path{treeDepth, sectorPosition};
        auto pathIterator = path.cbegin();
        tree_path::iterator pathEnd{path, sectorPosition.layer()};

        file::sector::handle parentSector;

        for (;;)
        {
            file::sector::handle sector;
            if (static_cast<uint64_t>(physId) >= mOwner.mArchive->size())
            {
                return error{archive_errc::sector_reference_out_of_range} << ed::sector_idx{physId};
            }

            const auto currentPosition = *pathIterator;
            if (auto entry = mCachedBlocks->access(
                    currentPosition, [&](void *mem) noexcept->result<file::sector *> {
                        auto xsec = new (mem) file::sector(parentSector, currentPosition, physId);
                        if (auto readResult = mOwner.mArchive->read_sector(xsec->data(), mData,
                                                                           physId, ro_dynblob(mac));
                            readResult.has_failure())
                        {
                            std::destroy_at(xsec);
                            readResult.assume_error() << ed::sector_idx{physId};
                            return std::move(readResult).as_failure();
                        }
                        return xsec;
                    }))
            {
                sector = std::move(entry).assume_value();
            }
            else
            {
                std::shared_lock fileLock{integrity_mutex};
                // if the file tree shrinks during an access operation it may happen
                // that one of the intermediate nodes dies :(
                // however this is detectable and recoverable if the cut off part
                // doesn't contain the block we want to access
                const auto nTreeDepth = mData.tree_depth;
                if (nTreeDepth < sectorPosition.layer())
                {
                    return error{archive_errc::sector_reference_out_of_range}
                           << ed::wrapped_error{std::move(entry).assume_error()};
                }
                if (treeDepth > nTreeDepth)
                {
                    return errc::device_busy;
                }

                // it can happen that the target sector is written to disk and and removed from the
                // cache after we obtained its mac in the last iteration, if we detect such case
                // we just reread the mac and (the maybe updated sector idx) and try again with them
                if (parentSector)
                {
                    fileLock.unlock();
                    // stabilize memory representation
                    std::lock_guard parentLock{parentSector->data_sync()};

                    auto &ref =
                        sector_creference_at(*parentSector, path.offset(sectorPosition.layer()));
                    if (ref.reference == sector_id::master)
                    {
                        return error{archive_errc::sector_reference_out_of_range}
                               << ed::wrapped_error{std::move(entry).assume_error()};
                    }
                    if (!equal(span(mac), span(ref.mac)))
                    {
                        physId = ref.reference;
                        mac = ref.mac;
                        continue;
                    }
                }
                else if (!equal(span{mac}, span{mData.start_block_mac}))
                {
                    return errc::device_busy;
                }
                return std::move(entry).as_failure();
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
                return error{archive_errc::sector_reference_out_of_range}
                       << ed::sector_idx{sector_id::master};
            }

            parentSector = std::move(sector);
        }
    }
    auto archive::file::access(tree_position sectorPosition) -> result<sector::handle>
    {
        if (!sectorPosition)
        {
            return errc::invalid_argument;
        }

        for (;;)
        {
            if (auto sector = access_impl(sectorPosition);
                !sector.has_error() || sector.assume_error() != errc::device_busy)
            {
                if (sector.has_error())
                {
                    sector.assume_error() << ed::sector_tree_position{sectorPosition}
                                          << ed::archive_file_id{mData.id};
                }
                return sector;
            }
            // continue to loop until it either fails with an error which
            // is not device_busy or until it succeeds
            std::this_thread::yield();
        }
    }

    result<void> archive::file::read(rw_dynblob buffer, std::uint64_t readPos)
    {
        auto offset = readPos % detail::raw_archive::sector_payload_size;
        tree_position it{detail::lut::sector_position_of(readPos)};

        while (buffer)
        {
            BOOST_OUTCOME_TRY(sectorHandle, access(it));
            it.position(it.position() + 1);

            auto chunk = sectorHandle->data_view().subspan(offset);
            offset = 0;

            std::shared_lock<std::shared_mutex> guard{sectorHandle->data_sync()};
            copy(chunk, buffer);
            buffer = buffer.subspan(std::min(chunk.size(), buffer.size()));
        }
        return outcome::success();
    }

    auto archive::file::access_or_append(const tree_position position) -> result<sector::handle>
    {
        using namespace detail;

        assert(position.layer() == 0);

        if (auto sector = mCachedBlocks->try_access(position))
        {
            return sector;
        }

        auto requiredDepth = lut::required_tree_depth(position.position());
        tree_path path{requiredDepth, position};

        std::shared_lock fileReadLock{integrity_mutex};
        const tree_position rootPos{0, std::max(mData.tree_depth, requiredDepth)};

        // check whether we need to increase the tree depth
        file::sector::handle parent;
        if (requiredDepth > mData.tree_depth)
        {
            fileReadLock.unlock();
            std::unique_lock fileWriteLock{integrity_mutex};

            if (requiredDepth > mData.tree_depth)
            {
                BOOST_OUTCOME_TRY(physId, mOwner.mFreeBlockIndexFile->alloc_sector());
                parent = mCachedBlocks->access(rootPos, parent, rootPos, physId);

                assert(physId == parent->sector_id());
                if (physId != parent->sector_id())
                {
                    // we got a cached sector entry, i.e. the previously
                    // allocated physical sector needs to be freed.
                    mOwner.mFreeBlockIndexFile->dealloc_sector(physId);

                    physId = parent->sector_id();
                }

                auto &ref = sector_reference_at(*parent, 0);
                ref.reference = mData.start_block_idx;
                ref.mac = mData.start_block_mac;

                mData.start_block_idx = physId;
                mData.start_block_mac = {};
                mData.tree_depth += 1;

                parent.mark_dirty();

                // update the old root if it is currently cached
                const tree_position oldRootPos{0, requiredDepth - 1};
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
                if ((parent = mCachedBlocks->try_access(tpos)))
                {
                    break;
                }
            }
            if (!parent)
            {
                if (auto acres = access(rootPos))
                {
                    parent = std::move(acres).assume_value();
                }
                else
                {
                    return std::move(acres).as_failure();
                }
            }
        }

        // #TODO #resilience consider implementing tree allocation rollback.

        // walk the tree path down to layer 0 inserting missing sectors
        // if parent is on layer 0 then `it` is automatically an end iterator
        for (auto it = tree_path::iterator{path, parent->position().layer() - 1}, end = path.cend();
             it != end; ++it)
        {
            auto &ref = sector_reference_at(*parent, it.array_offset());

            std::unique_lock parentLock{parent->data_sync()};
            if (ref.reference != sector_id::master)
            {
                parentLock.unlock();
                if (auto acres = access(*it))
                {
                    parent = std::move(acres).assume_value();
                }
                else
                {
                    return std::move(acres).as_failure();
                }
            }
            else
            {
                parentLock.unlock();

                BOOST_OUTCOME_TRY(physId, mOwner.mFreeBlockIndexFile->alloc_sector());
                sector::handle entry =
                    mCachedBlocks->access(*it, parent, *it, physId);

                if (physId != entry->sector_id())
                {
                    mOwner.mFreeBlockIndexFile->dealloc_sector(physId);
                }

                {
                    std::unique_lock entryLock{entry->data_sync(), std::defer_lock};
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

    result<void> archive::file::write(ro_dynblob data, std::uint64_t writeFilePos)
    {
        if (!data)
        {
            return outcome::success();
        }

        tree_position writePos{detail::lut::sector_position_of(writeFilePos)};
        auto offset = writeFilePos % detail::raw_archive::sector_payload_size;

        auto newMinSize = writeFilePos + data.size();

        std::shared_lock shrinkLock{shrink_mutex};
        // make sure that the file is valid up until our write starting point
        BOOST_OUTCOME_TRY(grow_file(writeFilePos + 1));

        while (data)
        {
            BOOST_OUTCOME_TRY(sectorHandle, access_or_append(writePos));
            writePos.position(writePos.position() + 1);

            auto amountWritten = write(sectorHandle, data, offset);
            offset = 0;

            data = data.subspan(amountWritten);

            auto newSize = std::min(writePos.position() * detail::raw_archive::sector_payload_size,
                                    newMinSize);

            std::lock_guard integrityLock{integrity_mutex};
            mData.size = std::max(mData.size, newSize);
        }

        return outcome::success();
    }

    std::uint64_t archive::file::write(sector::handle &sector, ro_dynblob data,
                                       std::uint64_t offset)
    {
        auto chunk = sector->data().subspan(offset);

        std::lock_guard sectorLock{sector->data_sync()};

        copy(data, chunk);

        sector.mark_dirty();
        mWriteFlag.mark();

        return std::min(chunk.size(), data.size());
    }

    std::uint64_t archive::file::write_no_lock(sector::handle &sector, ro_dynblob data,
                                               std::uint64_t offset)
    {
        auto chunk = sector->data().subspan(offset, data.size());

        copy(data, chunk);

        sector.mark_dirty();
        mWriteFlag.mark();

        return chunk.size();
    }

    result<void> archive::file::write_sector_to_disk(sector::handle sector)
    {
        if (!sector)
        {
            return outcome::success();
        }
        bool failed = false;

        std::shared_lock shrinkLock{shrink_mutex, std::defer_lock};
        std::unique_lock sectorLock{sector->data_sync(), std::defer_lock};
        std::lock(shrinkLock, sectorLock);
        VEFS_SCOPE_EXIT
        {
            if (failed)
                sector.mark_dirty();
        };
        VEFS_SCOPE_EXIT
        {
            sector->write_queued_flag().clear(std::memory_order_release);
        };

        if (!sector.is_dirty())
        {
            return outcome::success();
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
        auto ciphertext = span(encryptionMem).subspan<16>();
        auto mac = span(encryptionMem).first<16>();

        if (auto wx = mOwner.mArchive->write_sector(ciphertext, mac, mData, sector->sector_id(),
                                                    sector->data_view());
            !wx)
        {
            failed = true;
            return wx.as_failure();
        }

        // update parent sector with the new information
        // retry loop
        std::unique_lock fileIntegrityLock{integrity_mutex};

        auto parent = sector->parent();
        if (parent)
        {
            fileIntegrityLock.unlock();
            std::lock_guard parentLock{parent->data_sync()};

            auto offset = sector->position().parent_array_offset();
            auto &ref = sector_reference_at(*parent, offset);
            ref.reference = sector->sector_id();
            copy(mac, span(ref.mac));
            parent.mark_dirty();

            mHooks.on_sector_synced(ref.reference, mac);
        }
        else
        {
            assert(mData.tree_depth == sector->position().layer());

            mData.start_block_idx = sector->sector_id();
            copy(mac, mData.start_block_mac_blob());

            mHooks.on_root_sector_synced(mData);
        }
        sector.mark_clean();
        return outcome::success();
    }

    result<void> archive::file::resize(std::uint64_t size)
    {
        std::unique_lock shrinkLock{shrink_mutex};
        std::uint64_t fileSize;
        {
            std::unique_lock integrityLock{integrity_mutex};
            fileSize = mData.size;
        }

        if (fileSize < size)
        {
            shrinkLock.unlock();
            std::shared_lock growLock{shrink_mutex};
            return grow_file(size);
        }
        else if (fileSize > size)
        {
            return shrink_file(size);
        }
        return outcome::success();
    }

    result<void> archive::file::sync()
    {
        atomic_thread_fence(std::memory_order_acquire);

        auto layer = 0;
        bool dirtyElements;
        do
        {
            BOOST_OUTCOME_TRY(
                drx, mCachedBlocks->for_dirty(
                         [ this, layer ](block_pool_t::handle sector) noexcept->result<void> {
                             if (sector->position().layer() == layer)
                             {
                                 BOOST_OUTCOME_TRY(write_sector_to_disk(std::move(sector)));
                             }
                             return outcome::success();
                         }));
            dirtyElements = drx;
            layer = (layer + 1) % (detail::lut::max_tree_depth + 1);
        } while (dirtyElements);

        return outcome::success();
    }

    result<void> archive::file::erase_self()
    {
        std::unique_lock shrinkLock{shrink_mutex};
        BOOST_OUTCOME_TRY(shrink_file(0));

        std::unique_lock integrityLock{integrity_mutex};
        BOOST_OUTCOME_TRY(mOwner.mArchive->erase_sector(mData, mData.start_block_idx));
        mOwner.mFreeBlockIndexFile->dealloc_sector(mData.start_block_idx);
        mData.tree_depth = -1;
        mData.start_block_idx = detail::sector_id::master;
        mData.start_block_mac = {};

        return outcome::success();
    }

    result<void> archive::file::grow_file(std::uint64_t size)
    {
        using detail::raw_archive;

        auto endSectorPos = size ? (size - 1) / raw_archive::sector_payload_size : 0;
        std::uint64_t fileSize;
        {
            std::lock_guard<std::shared_mutex> integrityLock{integrity_mutex};
            fileSize = mData.size;
        }
        auto startSectorPos = fileSize ? (fileSize - 1) / raw_archive::sector_payload_size : 0;

        // the first sector is always allocated
        tree_position posIt{startSectorPos + 1};

        // the loop immediately terminates if the file is already big enough
        while (posIt.position() <= endSectorPos)
        {
            if (!is_allocated(fileSize, posIt))
            {
                BOOST_OUTCOME_TRY(access_or_append(posIt));

                auto newSize =
                    std::min((posIt.position() + 1) * raw_archive::sector_payload_size, size);

                std::lock_guard<std::shared_mutex> integrityLock{integrity_mutex};
                fileSize = mData.size = std::max(mData.size, newSize);
            }
            posIt.position(posIt.position() + 1);
        }

        {
            std::lock_guard<std::shared_mutex> integrityLock{integrity_mutex};
            if (mData.size < size)
            {
                mData.size = size;
            }
        }
        return outcome::success();
    }

    result<void> archive::file::shrink_file(const std::uint64_t size)
    {
        using detail::raw_archive;
        using detail::tree_path;
        namespace lut = detail::lut;

        std::uint64_t fileSize;
        int treeDepth;
        {
            std::lock_guard integrityLock{integrity_mutex};
            fileSize = mData.size;
            treeDepth = mData.tree_depth;
        }
        // we always keep the first sector alive
        if (fileSize <= raw_archive::sector_payload_size)
        {
            std::lock_guard integrityLock{integrity_mutex};
            mData.size = size;
            return outcome::success();
        }

        // #TODO batch small amounts of id releases
        // the amount of ids can grow rather large and failing a shrink because
        // of an bad_alloc from push_back is rather stupid
        std::vector<detail::sector_id> collectedIds;
        tree_path walker{treeDepth, lut::sector_position_of(fileSize - 1)};
        auto endPosition = size != 0 ? lut::sector_position_of(size) : 0;

        while (walker.position(0) > endPosition)
        {
            auto it = mCachedBlocks->try_access(walker.layer_position(0));
            detail::sector_id toBeCollected;

            if (it)
            {
                std::lock_guard writeLock{it->data_sync()};

                toBeCollected = it->sector_id();
                it.mark_clean();

                auto tmp = it->parent();
                it->update_parent({});
                it = std::move(tmp);
            }
            else
            {
                if (auto accrx = access(walker.layer_position(1)))
                {
                    it = std::move(accrx).assume_value();
                }
                else
                {
                    // #TODO failure recovery + cleanup of tree depth
                    return std::move(accrx).as_failure();
                }

                auto offset = walker.offset(0);
                auto ref = sector_reference_at(*it, offset);
                toBeCollected = ref.reference;
            }

            collectedIds.push_back(toBeCollected);
            BOOST_OUTCOME_TRY(mOwner.mArchive->erase_sector(mData, toBeCollected));

            // update all parent sectors affected by the removal of the current sector
            for (auto layer = 1;; ++layer)
            {
                std::lock_guard writeLock{it->data_sync()};

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
                    BOOST_OUTCOME_TRY(mOwner.mArchive->erase_sector(mData, sectorIdx));
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
            BOOST_OUTCOME_TRY(it, access(tree_position{0, adjustedDepth}));

            auto parent = it->parent();
            // #UB-ObjectLifetime
            auto ref = reinterpret_cast<RawSectorReference *>(parent->data().data());

            std::lock_guard integrityLock{integrity_mutex};
            mData.start_block_idx = ref->reference;
            mData.start_block_mac = ref->mac;
            mData.tree_depth = adjustedDepth;
            mData.size = size;

            // loop over all parents and free them
            do
            {
                it->update_parent({});

                it = std::move(parent);

                utils::secure_memzero(it->data().first<sizeof(RawSectorReference)>());
                auto sectorIdx = it->sector_id();
                collectedIds.push_back(sectorIdx);
                BOOST_OUTCOME_TRY(mOwner.mArchive->erase_sector(mData, sectorIdx));
                it.mark_clean();

                parent = it->parent();
            } while (parent);
        }
        else
        {
            std::lock_guard<std::shared_mutex> integrityLock{integrity_mutex};
            mData.size = size;
        }

        mOwner.mFreeBlockIndexFile->dealloc_sectors({collectedIds.data(), collectedIds.size()});
        return outcome::success();
    }
} // namespace vefs
