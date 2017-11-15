#pragma once

#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <limits>
#include <optional>
#include <string_view>

#include <boost/uuid/uuid.hpp>
#include <boost/functional/hash_fwd.hpp>

#include <vefs/blob.hpp>
#include <vefs/filesystem.hpp>
#include <vefs/crypto/provider.hpp>
#include <vefs/utils/misc.hpp>
#include <vefs/detail/raw_archive.hpp>
#include <vefs/detail/archive_file.hpp>
#include <vefs/detail/pool_cache.hpp>
#include <vefs/detail/thread_pool.hpp>

namespace vefs::detail
{
    class file_sector_id
    {
        using storage_type = std::uint64_t;
        static constexpr storage_type position_mask = 0x00FF'FFFF'FFFF'FFFF;
        static constexpr storage_type layer_mask = ~position_mask;
        static constexpr auto layer_offset = 56;

    public:
        static constexpr auto references_per_sector = raw_archive::sector_payload_size / 32;

        file_sector_id()
            : mFileId()
            , mLayerPosition(std::numeric_limits<storage_type>::max())
        {
        }
        file_sector_id(const file_id &fileId, std::uint64_t position, std::uint8_t layer)
            : mFileId(fileId)
            , mLayerPosition((static_cast<storage_type>(layer) << layer_offset) | (position & position_mask))
        {
        }

        const file_id & file_id() const noexcept
        {
            return mFileId;
        }

        std::uint8_t & layer() noexcept
        {
            return *(reinterpret_cast<std::uint8_t *>(&mLayerPosition) + 7);
        }
        std::uint8_t layer() const noexcept
        {
            return *(reinterpret_cast<const std::uint8_t *>(&mLayerPosition) + 7);
        }
        void layer(std::uint8_t value) noexcept
        {
            layer() = value;
        }

        std::uint64_t position() const noexcept
        {
            return mLayerPosition & position_mask;
        }
        void position(std::uint64_t value) noexcept
        {
            mLayerPosition = (mLayerPosition & layer_mask) | (value & position_mask);
        }
        std::size_t position_array_offset() const noexcept
        {
            return position() % references_per_sector;
        }

        storage_type layer_position() const noexcept
        {
            return mLayerPosition;
        }
        void layer_position(storage_type value)
        {
            mLayerPosition = value;
        }

        bool is_allocated(std::uint64_t fileSize) const
        {
            auto numAllocatedBlocks = utils::div_ceil(fileSize, raw_archive::sector_payload_size);
            auto l = layer();
            auto pos = position();
            if (l == 0)
            {
                return pos < numAllocatedBlocks
                    || (fileSize == 0 && pos == 0); // there is always a sector allocated for each file
            }
            auto width = utils::upow(references_per_sector, l);
            return numAllocatedBlocks > width && width * pos < numAllocatedBlocks;
        }

        explicit operator bool() const noexcept
        {
            return mFileId && mLayerPosition != std::numeric_limits<storage_type>::max();
        }

        file_sector_id parent() const noexcept
        {
            auto l = layer();
            l += 1;
            return file_sector_id{ mFileId, position() / references_per_sector, l };
        }

    private:
        vefs::detail::file_id mFileId;
        // 8b layer pos + 56b sector position on that layer
        storage_type mLayerPosition;
    };


    class file_sector
    {
    public:
        const auto & sector() const noexcept
        {
            return mSector;
        }

        const auto & id() const noexcept
        {
            return mId;
        }

        auto data() noexcept
        {
            return blob{ mBlockData };
        }
        auto data() const noexcept
        {
            return blob_view{ mBlockData };
        }
        auto data_view() const noexcept
        {
            return blob_view{ mBlockData };
        }

        auto & dirty_flag()
        {
            return mDirtyFlag;
        }
        const auto & dirty_flag() const
        {
            return mDirtyFlag;
        }

        auto & write_mutex()
        {
            return mWriteMutex;
        }
        auto & write_queued_flag()
        {
            return mWriteQueued;
        }

        file_sector(file_sector_id logicalId, sector_id physId) noexcept
            : mId(logicalId)
            , mSector(physId)
            , mWriteMutex()
            , mDirtyFlag(false)
        {
            utils::secure_memzero(data());
        }
        file_sector(detail::raw_archive &src, const detail::raw_archive_file &file,
                const file_sector_id &logicalId, sector_id physId, blob_view mac)
            : file_sector(logicalId, physId)
        {
            src.read_sector(data(), file, mSector, mac);
        }

    private:
        file_sector_id mId;
        detail::sector_id mSector;
        std::mutex mWriteMutex;
        std::atomic<bool> mDirtyFlag;
        std::atomic_flag mWriteQueued = ATOMIC_FLAG_INIT;
        std::array<std::byte, detail::raw_archive::sector_payload_size> mBlockData;
    };

    inline bool operator==(const file_sector_id &lhs, const file_sector_id &rhs)
    {
        return lhs.file_id() == rhs.file_id() && lhs.layer_position() == rhs.layer_position();
    }
    inline bool operator!=(const file_sector_id &lhs, const file_sector_id &rhs)
    {
        return !(lhs == rhs);
    }

    std::string to_string(const file_sector_id &id);
}

namespace std
{
    template <>
    struct hash<vefs::detail::file_sector_id>
    {
        std::size_t operator()(const vefs::detail::file_sector_id &id)
        {
            //TODO: do something better than this!
            static_assert(sizeof(vefs::detail::file_sector_id) == 24);
            auto begin = reinterpret_cast<const std::uint64_t *>(&id);
            auto end = begin + sizeof(vefs::detail::file_sector_id) / sizeof(std::uint64_t);
            return boost::hash_range(begin, end);
        }
    };
}

namespace vefs
{
    class archive
    {
        using file_sector = detail::file_sector;
        using file_sector_handle = std::shared_ptr<file_sector>;
        using block_pool_t = detail::caching_object_pool<detail::file_sector_id, file_sector, 1 << 10>;
        using raw_archive_file_ptr = std::shared_ptr<detail::raw_archive_file>;

        class file_walker;
        class writer_task;

    public:
        using create_tag = detail::raw_archive::create_tag;
        static constexpr auto create = create_tag{};

        archive(filesystem::ptr fs, std::string_view archivePath,
            crypto::crypto_provider *cryptoProvider, blob_view userPRK);
        archive(filesystem::ptr fs, std::string_view archivePath,
            crypto::crypto_provider *cryptoProvider, blob_view userPRK, create_tag);
        ~archive();

        void sync();

        detail::file_id open(std::string_view filePath, file_open_mode_bitset mode);
        void erase(std::string_view filePath);
        void read(detail::file_id id, blob buffer, std::uint64_t readFilePos);
        void write(detail::file_id id, blob_view data, std::uint64_t writeFilePos);
        void resize(detail::file_id id, std::uint64_t size);
        std::uint64_t size_of(detail::file_id id);
        void sync(detail::file_id id);

    private:
        archive();

        void write(detail::raw_archive_file &file, blob_view data, std::uint64_t writeFilePos);
        // the caller is required to hold the shrink lock during the grow_file() call
        void grow_file(detail::raw_archive_file &file, std::uint64_t size);
        // the caller is required to uniquely lock the shrink mutex during the call
        void shrink_file(detail::raw_archive_file &file, const std::uint64_t size);

        void read_archive_index();
        void write_archive_index();
        void read_free_sector_index();
        void write_free_sector_index();

        std::shared_ptr<file_sector> access_or_append(detail::raw_archive_file &file, detail::file_sector_id id);

        std::map<detail::sector_id, std::uint64_t>::iterator grow_archive_impl(unsigned int num);
        std::vector<detail::sector_id> alloc_sectors(unsigned int num = 1);
        inline auto alloc_sector()
        {
            return alloc_sectors(1).front();
        }
        void dealloc_sectors(std::vector<detail::sector_id> sectors);
        void dealloc_sectors_impl(std::vector<detail::sector_id> sectors);

        void mark_dirty(const std::shared_ptr<file_sector> &sector)
        {
            if (!sector->dirty_flag().exchange(true, std::memory_order_acq_rel))
            {
                ++mDirtyObjects;
            }
            mBlockPool->mark_as_accessed(sector->id());
        }

        raw_archive_file_ptr get_file_handle(const detail::file_id &id)
        {
            using namespace detail;
            if (id == file_id::archive_index)
            {
                return { &mArchive->index_file(), [](auto) {} };
            }
            else if (id == file_id::free_block_index)
            {
                return { &mArchive->free_sector_index_file(), [](auto) {} };
            }
            return mFiles.find(id);
        }
        raw_archive_file_ptr get_file_handle(const detail::file_sector_id &fsid)
        {
            return get_file_handle(fsid.file_id());
        }

        std::shared_ptr<file_sector> safe_acquire(const detail::raw_archive_file &file,
            const detail::file_sector_id &logicalId, detail::sector_id physId, blob_view mac);

        file_sector_handle access(const detail::raw_archive_file &file, detail::file_sector_id sector);
        std::optional<file_sector_handle> access_impl(const detail::raw_archive_file &file,
            const detail::file_sector_id &sector);

        std::unique_ptr<detail::raw_archive> mArchive;

        std::unique_ptr<block_pool_t> mBlockPool;
        cuckoohash_map<std::string, detail::file_id, std::hash<std::string_view>, std::equal_to<>> mIndex;
        cuckoohash_map<detail::file_id, raw_archive_file_ptr> mFiles;

        std::unique_ptr<detail::thread_pool> mOpsPool = std::make_unique<detail::thread_pool>();

        std::atomic<unsigned int> mDirtyObjects;

        std::map<detail::sector_id, std::uint64_t> mFreeSectorPool;
        std::mutex mFreeSectorPoolMutex;
    };
}
