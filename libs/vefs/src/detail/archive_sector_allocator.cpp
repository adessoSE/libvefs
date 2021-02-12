#include "archive_sector_allocator.hpp"

#include <cassert>

#include <vefs/utils/binary_codec.hpp>

#include "preallocated_tree_allocator.hpp"
#include "sector_tree_seq.hpp"
#include "tree_lut.hpp"

namespace vefs::detail
{
#pragma region free block sector codec

    struct free_block_range
    {
        free_block_range() noexcept = default;
        explicit free_block_range(utils::id_range<sector_id> range) noexcept
            : start_id(range.first())
            , num_sectors(range.size())
        {
        }

        sector_id start_id;
        std::uint64_t num_sectors;
    };

    class free_block_sector_layout
    {
    public:
        static constexpr std::size_t serialized_block_range_size = 16;
        static constexpr std::size_t num_entries_per_sector =
            sector_device::sector_payload_size / serialized_block_range_size;

        explicit inline free_block_sector_layout(
            rw_blob<sector_device::sector_payload_size> data) noexcept
            : mCodec(data)
        {
        }

        inline auto read(const int which) const noexcept -> free_block_range
        {
            const auto baseOffset =
                static_cast<std::size_t>(which) * serialized_block_range_size;

            free_block_range deserialized;
            deserialized.start_id = mCodec.read<sector_id>(baseOffset);
            deserialized.num_sectors = mCodec.read<std::uint64_t>(
                baseOffset + sizeof(deserialized.start_id));

            return deserialized;
        }
        inline void write(const int which,
                          const free_block_range range) noexcept
        {
            const auto baseOffset =
                static_cast<std::size_t>(which) * serialized_block_range_size;

            mCodec.write(range.start_id, baseOffset);
            mCodec.write(range.num_sectors,
                         baseOffset + sizeof(range.start_id));
        }

    private:
        utils::binary_codec<sector_device::sector_payload_size> mCodec;
    };

#pragma endregion

    archive_sector_allocator::archive_sector_allocator(
        sector_device &device, file_crypto_ctx::state_type const &cryptoCtx)
        : mSectorDevice(device)
        , mSectorManager()
        , mAllocatorSync()
        , mFileCtx(cryptoCtx)
        , mFreeBlockFileRootSector()
        , mSectorsLeaked(false)
    {
    }

    auto archive_sector_allocator::alloc_one() noexcept -> result<sector_id>
    {
        std::lock_guard allocGuard{mAllocatorSync};

        if (auto allocationrx = mSectorManager.alloc_one();
            allocationrx ||
            allocationrx.assume_error() != errc::resource_exhausted)
        {
            return allocationrx;
        }

        VEFS_TRY(mine_new(4));

        return mSectorManager.alloc_one();
    }

    auto archive_sector_allocator::dealloc_one(sector_id which) noexcept
        -> result<void>
    {
        return mSectorManager.dealloc_one(which);
    }
    void archive_sector_allocator::dealloc_one(sector_id which,
                                               leak_on_failure_t) noexcept
    {
        if (!dealloc_one(which))
        {
            on_leak_detected();
        }
    }

    auto archive_sector_allocator::merge_from(
        utils::block_manager<sector_id> &other) noexcept -> result<void>
    {
        std::lock_guard lock{mAllocatorSync};
        return mSectorManager.merge_from(other);
    }
    auto archive_sector_allocator::merge_disjunct(
        utils::block_manager<sector_id> &other) noexcept -> result<void>
    {
        std::lock_guard lock{mAllocatorSync};
        return mSectorManager.merge_disjunct(other);
    }

    auto archive_sector_allocator::mine_new_raw(int num) noexcept
        -> result<id_range>
    {
        assert(num > 0);
        using id_range_t = utils::id_range<sector_id>;

        auto oldSize = mSectorDevice.size();
        if (auto resizerx = mSectorDevice.resize(oldSize + num); !resizerx)
        {
            return error(errc::resource_exhausted)
                   << ed::wrapped_error(std::move(resizerx).assume_error());
        }
        sector_id first{oldSize};
        return id_range_t{first, id_range_t::advance(first, num)};
    }

    auto archive_sector_allocator::mine_new(int num) noexcept -> result<void>
    {
        assert(num > 0);

        VEFS_TRY(allocated, mine_new_raw(num));

        if (auto insertrx =
                mSectorManager.dealloc_contiguous(allocated.first(), num);
            !insertrx)
        {
            if (auto shrinkrx = mSectorDevice.resize(
                    static_cast<uint64_t>(allocated.first()));
                shrinkrx.has_failure())
            {
                // can't keep track of the newly allocated sectors
                // neither the manager has space nor could we deallocate
                // them, therefore we leak them until the recovery is
                // invoked
                on_leak_detected();

                shrinkrx.assume_error()
                    << ed::wrapped_error(std::move(insertrx).assume_error());
                return std::move(shrinkrx).assume_error();
            }
            return std::move(insertrx).as_failure();
        }
        return success();
    }

    auto archive_sector_allocator::initialize_new() noexcept -> result<void>
    {
        VEFS_TRY(newRoot, alloc_one());
        mFreeBlockFileRootSector = newRoot;

        return success();
    }

    auto archive_sector_allocator::initialize_from(
        root_sector_info rootInfo) noexcept -> result<void>
    {
        using file_tree_allocator = preallocated_tree_allocator;
        using file_tree = sector_tree_seq<file_tree_allocator>;

        sector_id const fileEndId{mSectorDevice.size()};

        file_tree_allocator::sector_id_container idContainer;
        VEFS_TRY(freeSectorTree,
                 file_tree::open_existing(mSectorDevice, mFileCtx, rootInfo,
                                          idContainer));

        const auto lastSectorPos =
            (rootInfo.maximum_extent - 1) / sector_device::sector_payload_size;
        if (lastSectorPos != 0)
        {
            VEFS_TRY(freeSectorTree->move_to(lastSectorPos));
        }

        // > To write optimal code, always start with an infinite loop.
        // Andrei Alexandrescu
        for (;;)
        {
            free_block_sector_layout sector{freeSectorTree->writeable_bytes()};
            for (int i = 0; i < sector.num_entries_per_sector; ++i)
            {
                auto [startId, numSectors] = sector.read(i);
                if (startId == sector_id{})
                {
                    // sentinel
                    break;
                }
                if (auto lastId = id_range::advance(startId, numSectors - 1);
                    lastId < startId || !(lastId < fileEndId))
                {
                    // overflow => invalid range
                    continue;
                }
                VEFS_TRY(
                    mSectorManager.dealloc_contiguous(startId, numSectors));
            }

            if (const auto currentLeaf = freeSectorTree->position().position())
            {
                VEFS_TRY(freeSectorTree->erase_leaf(currentLeaf));
                for (auto id : idContainer)
                {
                    VEFS_TRY(mSectorManager.dealloc_one(id));
                }
                idContainer.clear();

                VEFS_TRY(freeSectorTree->move_backward());
            }
            else
            {
                break;
            }
        }

        VEFS_TRY(freeSectorTree->erase_self());
        mFreeBlockFileRootSector = idContainer.front();
        return success();
    }

    static auto
    num_required_storage_sectors(utils::block_manager<sector_id> &sectorManager)
        -> std::uint64_t
    {
        return lut::required_sector_count(
            sectorManager.num_nodes() *
            free_block_sector_layout::serialized_block_range_size);
    }

    static auto preallocate_serialization_storage(
        sector_id rootSectorId, utils::block_manager<sector_id> &sectorManager,
        preallocated_tree_allocator::sector_id_container &idContainer) noexcept
        -> result<void>
    {
        auto numStorageSectors = num_required_storage_sectors(sectorManager);
        if (numStorageSectors >= std::numeric_limits<std::size_t>::max())
        {
            return errc::not_enough_memory;
        }

        try
        {
            idContainer.push_back(rootSectorId);
            if (numStorageSectors == 1)
            {
                return success();
            }
            idContainer.resize(numStorageSectors,
                               boost::container::default_init);
        }
        catch (const std::bad_alloc &)
        {
            return errc::not_enough_memory;
        }

        // numStorageSectors < num_nodes()
        // => numAllocated == numStorageSectors
        VEFS_TRY(sectorManager.alloc_multiple(span(idContainer).subspan<1>()));

        // allocating sectors for the free block file, can reduce the size of
        // said file in certain edge cases which in turn may produce some empty
        // trailing data nodes. Therefore we do a balancing pass in order to
        // minimize the amount of trailing sectors
        if (auto adjustedStorageSectors =
                num_required_storage_sectors(sectorManager);
            adjustedStorageSectors < numStorageSectors)
        {
            for (auto id : span(idContainer).subspan(adjustedStorageSectors))
            {
                VEFS_TRY(sectorManager.dealloc_one(id));
            }

            numStorageSectors = num_required_storage_sectors(sectorManager);
            idContainer.resize(numStorageSectors,
                               boost::container::default_init);

            if (auto missing =
                    span(idContainer).subspan(adjustedStorageSectors))
            {
                // numStorageSectors < num_nodes()
                // => numAllocated == numStorageSectors
                VEFS_TRY(sectorManager.alloc_multiple(missing));
            }
        }

        return success();
    }

    auto archive_sector_allocator::finalize(
        file_crypto_ctx const &filesystemCryptoCtx,
        root_sector_info filesystemRoot) noexcept -> result<void>
    {
        using file_tree_allocator = preallocated_tree_allocator;
        using file_tree = sector_tree_seq<file_tree_allocator>;

        std::lock_guard lock{mAllocatorSync};
        VEFS_TRY(trim());

        file_tree_allocator::sector_id_container idContainer;
        VEFS_TRY(preallocate_serialization_storage(
            mFreeBlockFileRootSector, mSectorManager, idContainer));

        VEFS_TRY(freeSectorTree,
                 file_tree::create_new(mSectorDevice, mFileCtx, idContainer));

        auto offset = 0;
        free_block_sector_layout sector{freeSectorTree->writeable_bytes()};
        for (auto freeRange : mSectorManager)
        {
            if (offset == sector.num_entries_per_sector)
            {
                offset = 0;
                VEFS_TRY(freeSectorTree->move_forward(
                    file_tree::access_mode::force));
                sector =
                    free_block_sector_layout{freeSectorTree->writeable_bytes()};
            }

            sector.write(offset, free_block_range{freeRange});
            offset += 1;
        }

        for (;;)
        {
            VEFS_TRY(freeSectorTree->commit(
                [&](root_sector_info rootInfo) noexcept -> result<void> {
                    if (!idContainer.empty())
                    {
                        return success();
                    }
                    rootInfo.maximum_extent =
                        (freeSectorTree->position().position() + 1) *
                        sector_device::sector_payload_size;

                    VEFS_TRY(mSectorDevice.update_header(filesystemCryptoCtx,
                                                         filesystemRoot,
                                                         mFileCtx, rootInfo));
                    return success();
                }));

            if (idContainer.empty())
            {
                return success();
            }
            else
            {
                VEFS_TRY(freeSectorTree->move_forward(
                    file_tree::access_mode::force));
            }
        }
    }

    auto archive_sector_allocator::trim() noexcept -> result<void>
    {
        auto oldSize = mSectorDevice.size();
        auto numTrimmed = mSectorManager.trim_ids(sector_id{oldSize});

        if (numTrimmed > 0)
        {
            return mSectorDevice.resize(oldSize - numTrimmed);
        }
        return success();
    }
} // namespace vefs::detail
