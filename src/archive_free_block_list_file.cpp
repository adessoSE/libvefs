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
        using detail::sector_id;

        if (mData.size % sizeof(RawFreeSectorRange) != 0)
        {
            BOOST_THROW_EXCEPTION(archive_corrupted{}
                << errinfo_code{ archive_error_code::free_sector_index_invalid_size }
            );
        }

        std::lock_guard<std::mutex> freeSectorLock{ mFreeBlockSync };
        tree_position it{ 0 };

        for (std::uint64_t consumed = 0; consumed < mData.size; )
        {
            auto sector = access(it);
            it.position(it.position() + 1);

            auto sectorBlob = sector->data_view();
            if (mData.size - consumed < detail::raw_archive::sector_payload_size)
            {
                sectorBlob.slice(0, static_cast<std::size_t>(mData.size - consumed));
            }
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
    }
    archive::free_block_list_file::free_block_list_file(archive &owner,
        detail::basic_archive_file_meta &meta, create_tag)
        : file_events{}
        , archive::internal_file{ owner, meta, *this }
        , mFreeBlockSync{}
        , mFreeBlockMap{}
    {
        tree_position rootPos{ 0 };
        auto physId = alloc_sector();
        auto entry = mCachedBlocks->access(rootPos, sector::handle{}, rootPos, physId);
        std::get<sector::handle>(entry).mark_dirty();

        mData.start_block_idx = physId;
        mData.start_block_mac = {};
        mData.tree_depth = 0;
    }

    std::vector<detail::sector_id> archive::free_block_list_file::alloc_sectors(unsigned int num)
    {
        using detail::sector_id;

        std::vector<sector_id> allocated;
        allocated.reserve(num);

        std::lock_guard<std::mutex> allocLock{ mFreeBlockSync };

        VEFS_ERROR_EXIT{
            try
            {
                dealloc_sectors_impl(std::move(allocated));
            }
            catch (...)
            {
            }
        };

        auto freeSectorIt = mFreeBlockMap.begin();
        auto freeSectorRangeEnd = mFreeBlockMap.end();
        while (num)
        {
            if (freeSectorIt == freeSectorRangeEnd)
            {
                freeSectorIt = grow_owner_impl(std::min(4u, num));
            }
            auto &[lastIdx, offset] = *freeSectorIt;

            auto i = 0ull;
            for (; num && i <= offset; ++i)
            {
                allocated.push_back(sector_id{ static_cast<std::uint64_t>(lastIdx) - offset + i });
                --num;
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
        return allocated;
    }

    void archive::free_block_list_file::dealloc_sectors(std::vector<detail::sector_id> sectors)
    {
        using detail::sector_id;

        if (sectors.empty())
        {
            return;
        }

        std::sort(sectors.begin(), sectors.end());
        auto newEnd = std::unique(sectors.begin(), sectors.end());
        sectors.resize(std::distance(sectors.begin(), newEnd));

        if (sectors.front() == sector_id::master)
        {
            sectors.erase(sectors.cbegin());
            if (sectors.empty())
            {
                return;
            }
        }

        std::lock_guard<std::mutex> allocLock{ mFreeBlockSync };

        dealloc_sectors_impl(std::move(sectors));
        mWriteFlag.mark();
    }

    void archive::free_block_list_file::sync()
    {
        if (!mWriteFlag.is_dirty())
        {
            return;
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
                    shrink_file(
                        (mFreeBlockMap.size() + 2) * sizeof(RawFreeSectorRange)
                    );
                }
                freeSectorLock.lock();
            }

            if (mData.size / sizeof(RawFreeSectorRange) < mFreeBlockMap.size())
            {
                freeSectorLock.unlock();
                {
                    std::shared_lock<std::shared_mutex> shrinkLock{ shrink_mutex };

                    grow_file(
                        mFreeBlockMap.size() * sizeof(RawFreeSectorRange)
                    );
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

            write(entryView, writePos);
            writePos += entryView.size();
        }

        entry = {};
        while (writePos < mData.size)
        {
            write(entryView, writePos);
            writePos += entryView.size();
        }

        internal_file::sync();
    }

    auto archive::free_block_list_file::grow_owner_impl(unsigned int num)
        -> archive::free_block_list_file::free_block_map::iterator
    {
        if (!num)
        {
            return {};
        }
        // assuming mFreeBlockSync is held
        num -= 1;
        auto newLastSector = detail::sector_id{ mOwner.mArchive->size() + num };
        mOwner.mArchive->resize(static_cast<std::uint64_t>(newLastSector) + 1);

        return mFreeBlockMap.emplace_hint(mFreeBlockMap.cend(), newLastSector, num);
    }

    void archive::free_block_list_file::dealloc_sectors_impl(std::vector<detail::sector_id> sectors)
    {
        auto current = sectors.front();
        auto offset = 0ull;
        for (auto it = ++sectors.cbegin(), end = sectors.cend(); it != end; ++it)
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
