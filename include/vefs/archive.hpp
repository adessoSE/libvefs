#pragma once

#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <limits>
#include <optional>
#include <string_view>

#include <vefs/blob.hpp>
#include <vefs/filesystem.hpp>
#include <vefs/crypto/provider.hpp>
#include <vefs/detail/raw_archive.hpp>
#include <vefs/detail/archive_file.hpp>
#include <vefs/detail/pool_cache.hpp>
#include <vefs/detail/thread_pool.hpp>
#include <vefs/detail/file_sector.hpp>


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

        file_sector::handle access_or_append(detail::raw_archive_file &file, std::uint64_t position);

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
