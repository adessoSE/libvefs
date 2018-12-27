#include "precompiled.hpp"
#include "archive_free_block_list_file.hpp"

#include <vefs/utils/misc.hpp>

namespace vefs
{
    namespace
    {
#pragma pack(push, 1)

        struct RawFreeSectorRange
        {
            detail::sector_id start_sector;
            std::uint64_t num_sectors;
        };
        static_assert(sizeof(RawFreeSectorRange) == 16);

#pragma pack(pop)
    }

    archive::free_block_list_file::free_block_list_file(archive &owner,
        detail::basic_archive_file_meta &meta)
        : file_events{}
        , archive::internal_file{ owner, meta, *this }
        , mFreeBlockSync{}
        , mFreeBlockMap{}
    {
    }

    auto archive::free_block_list_file::open(archive & owner, detail::basic_archive_file_meta &meta)
        -> result<std::shared_ptr<free_block_list_file>>
    {
        return internal_file::open<free_block_list_file>(owner, meta);
    }

    auto archive::free_block_list_file::create_new(archive & owner, detail::basic_archive_file_meta &meta)
        -> result<std::shared_ptr<free_block_list_file>>
    {
        auto self = std::make_shared<free_block_list_file>(owner, meta);

        tree_position rootPos{ 0 };
        OUTCOME_TRY(physId, self->alloc_sector());
        OUTCOME_TRY(entry,
            self->mCachedBlocks->access_w_inplace_ctor(rootPos, sector::handle{}, rootPos, physId));
        entry.mark_dirty();

        self->mData.start_block_idx = physId;
        self->mData.start_block_mac = {};
        self->mData.tree_depth = 0;

        return std::move(self);
    }

    auto archive::free_block_list_file::alloc_sectors(basic_range<detail::sector_id> dest)
        -> result<void>
    {
        using detail::sector_id;
        if (!dest)
        {
            return outcome::success();
        }

        auto out = dest.data();
        const auto end = out + dest.size();

        std::lock_guard<std::mutex> allocLock{ mFreeBlockSync };

        auto freeSectorIt = mFreeBlockMap.begin();
        const auto freeSectorRangeEnd = mFreeBlockMap.end();
        while (out != end)
        {
            if (freeSectorIt == freeSectorRangeEnd)
            {
                if (auto growrx = grow_owner_impl(std::min<std::ptrdiff_t>(4, end - out)))
                {
                    freeSectorIt = std::move(growrx).assume_value();
                }
                else
                {
                    dealloc_sectors_impl({ dest.data(), out - dest.data() });
                    return std::move(growrx).as_failure();
                }
            }
            auto &[lastIdx, offset] = *freeSectorIt;

            std::uint64_t i{};
            for (; out != end && i <= offset; ++i)
            {
                *out++ = sector_id{ static_cast<std::uint64_t>(lastIdx) - offset + i };
            }

            if (i > offset)
            {
                mFreeBlockMap.erase(freeSectorIt++);
            }
            else
            {
                offset -= i;
            }
        }

        mWriteFlag.mark();
        return outcome::success();
    }

    void archive::free_block_list_file::dealloc_sector(detail::sector_id sector)
    {
        if (sector == detail::sector_id::master)
        {
            return;
        }

        std::lock_guard<std::mutex> allocLock{ mFreeBlockSync };

        dealloc_sectors_impl({ &sector, 1 });
        mWriteFlag.mark();
    }

    void archive::free_block_list_file::dealloc_sectors(basic_range<detail::sector_id> sectors)
    {
        using detail::sector_id;

        if (sectors.empty())
        {
            return;
        }

        auto beg = sectors.data();
        auto end = beg + sectors.size();

        // sort all sector ids to be freed
        std::sort(beg, end);
        // eliminate duplicate ids
        sectors = sectors.slice(0, std::unique(beg, end) - beg);
        // remove the master sector (=0) value (if necessary)
        sectors = sectors.slice(sectors.front() == sector_id::master);

        // range contained only zeroes
        if (sectors.empty())
        {
            return;
        }

        std::lock_guard<std::mutex> allocLock{ mFreeBlockSync };

        dealloc_sectors_impl(sectors);
        mWriteFlag.mark();
    }

    auto archive::free_block_list_file::sync()
        -> result<void>
    {
        if (!mWriteFlag.is_dirty())
        {
            return outcome::success();
        }

        using detail::sector_id;

        RawFreeSectorRange entry = {};
        const blob_view entryView = as_blob_view(entry);
        std::uint64_t writePos = 0;

        std::unique_lock<std::mutex> freeSectorLock{ mFreeBlockSync };

        // #TODO there must be a better way to reliably flush the free block index
        do
        {
            while (mData.size / sizeof(RawFreeSectorRange) > mFreeBlockMap.size() + 2)
            {
                freeSectorLock.unlock();
                {
                    std::unique_lock<std::shared_mutex> shrinkLock{ shrink_mutex };
                    OUTCOME_TRY(shrink_file(
                        (mFreeBlockMap.size() + 2) * sizeof(RawFreeSectorRange)
                    ));
                }
                freeSectorLock.lock();
            }

            if (mData.size / sizeof(RawFreeSectorRange) < mFreeBlockMap.size())
            {
                freeSectorLock.unlock();
                {
                    std::shared_lock<std::shared_mutex> shrinkLock{ shrink_mutex };

                    OUTCOME_TRY(grow_file(
                        mFreeBlockMap.size() * sizeof(RawFreeSectorRange)
                    ));
                }
                freeSectorLock.lock();
            }
            else
            {
                break;
            }
        } while (true);

        for (auto[lastId, numPrev] : mFreeBlockMap)
        {
            entry.start_sector = sector_id{ static_cast<uint64_t>(lastId) - numPrev };
            entry.num_sectors = numPrev + 1;

            OUTCOME_TRY(write(entryView, writePos));
            writePos += entryView.size();
        }

        entry = {};
        while (writePos < mData.size)
        {
            OUTCOME_TRY(write(entryView, writePos));
            writePos += entryView.size();
        }

        OUTCOME_TRY(internal_file::sync());
        return outcome::success();
    }

    auto archive::free_block_list_file::parse_content()
        -> result<void>
    {
        using detail::sector_id;

        if (mData.size % sizeof(RawFreeSectorRange) != 0)
        {
            return archive_errc::free_sector_index_invalid_size;
        }

        std::lock_guard<std::mutex> freeSectorLock{ mFreeBlockSync };
        tree_position it{ 0 };

        for (std::uint64_t consumed = 0; consumed < mData.size; )
        {
            OUTCOME_TRY(sectorHandle, access(it));
            it.position(it.position() + 1);

            auto sectorBlob = sectorHandle->data_view()
                .slice(0, static_cast<std::size_t>(mData.size - consumed));
            consumed += sectorBlob.size();

            while (sectorBlob)
            {
                auto &freeSectorRange = sectorBlob.pop_front_as<RawFreeSectorRange>();
                if (freeSectorRange.start_sector == sector_id::master)
                {
                    continue;
                }

                auto sectorId = static_cast<std::uint64_t>(freeSectorRange.start_sector);

                auto offset = freeSectorRange.num_sectors - 1;
                auto lastSector = sector_id{ sectorId + offset };

                mFreeBlockMap.emplace_hint(mFreeBlockMap.cend(), lastSector, offset);
            }
        }
        return outcome::success();
    }

    auto archive::free_block_list_file::grow_owner_impl(std::uint64_t num)
        -> result<free_block_map::iterator>
    {
        if (!num)
        {
            return outcome::success();
        }
        // assuming mFreeBlockSync is held
        num -= 1;
        auto newLastSector = detail::sector_id{ mOwner.mArchive->size() + num };
        OUTCOME_TRY(mOwner.mArchive->resize(static_cast<std::uint64_t>(newLastSector) + 1));

        return mFreeBlockMap.emplace_hint(mFreeBlockMap.cend(), newLastSector, num);
    }

    void archive::free_block_list_file::dealloc_sectors_impl(basic_range<detail::sector_id> sectors)
    {
        assert(!sectors.empty());

        auto current = sectors.front();
        std::uint64_t offset = 0;
        for (auto it = sectors.data() + 1, end = it + sectors.size(); it != end; ++it)
        {
            const auto next = *it;
            if (static_cast<std::uint64_t>(next) - static_cast<std::uint64_t>(current) == 1)
            {
                ++offset;
            }
            else
            {
                mFreeBlockMap.emplace(current, offset);
                offset = 0;
            }
            current = next;
        }
        mFreeBlockMap.emplace(current, offset);
    }

    void archive::free_block_list_file::on_sector_write_suggestion(sector_handle sector)
    {
        on_dirty_sector(std::move(sector));
    }
    void archive::free_block_list_file::on_root_sector_synced(
        detail::basic_archive_file_meta &rootMeta)
    {
    }
    void archive::free_block_list_file::on_sector_synced(detail::sector_id physId,
        blob_view mac)
    {
    }
}
