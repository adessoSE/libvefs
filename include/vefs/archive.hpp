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
#include <vefs/detail/sector_id.hpp>
#include <vefs/detail/tree_walker.hpp>
#include <vefs/detail/raw_archive.hpp>
#include <vefs/detail/basic_archive_file_meta.hpp>
#include <vefs/detail/cache.hpp>
#include <vefs/utils/unordered_map_mt.hpp>
#include <vefs/detail/thread_pool.hpp>


namespace vefs
{
    class archive
    {
        using raw_archive_file_ptr = std::unique_ptr<detail::basic_archive_file_meta>;

        class file_walker;
        class writer_task;
    public:
        using create_tag = detail::raw_archive::create_tag;
        static constexpr auto create = create_tag{};

        class file;
        using file_handle = std::shared_ptr<file>;

    private:
        struct file_lookup
        {
            inline file_lookup(raw_archive_file_ptr first)
                : persistent{ std::move(first) }
                , handle{}
            {
            }

            inline file_handle to_handle(archive &owner);

            raw_archive_file_ptr persistent;
            file_handle handle;
        };

    public:

        archive(filesystem::ptr fs, std::string_view archivePath,
            crypto::crypto_provider *cryptoProvider, blob_view userPRK);
        archive(filesystem::ptr fs, std::string_view archivePath,
            crypto::crypto_provider *cryptoProvider, blob_view userPRK, create_tag);
        ~archive();

        void sync();

        file_handle open(std::string_view filePath, file_open_mode_bitset mode);
        void close(file_handle &handle);
        void erase(std::string_view filePath);
        void read(file_handle handle, blob buffer, std::uint64_t readFilePos);
        void write(file_handle handle, blob_view data, std::uint64_t writeFilePos);
        void resize(file_handle handle, std::uint64_t size);
        std::uint64_t size_of(file_handle handle);
        void sync(file_handle handle);

    private:
        archive();

        void read_archive_index();
        void write_archive_index();
        void read_free_sector_index();
        void write_free_sector_index();

        std::map<detail::sector_id, std::uint64_t>::iterator grow_archive_impl(unsigned int num);
        std::vector<detail::sector_id> alloc_sectors(unsigned int num = 1);
        inline auto alloc_sector()
        {
            return alloc_sectors(1).front();
        }
        void dealloc_sectors(std::vector<detail::sector_id> sectors);
        void dealloc_sectors_impl(std::vector<detail::sector_id> sectors);

        std::unique_ptr<detail::raw_archive> mArchive;

        utils::unordered_string_map_mt<detail::file_id> mIndex;
        utils::unordered_map_mt<detail::file_id, file_lookup> mFileHandles;
        file_handle mArchiveIndexFile;
        file_handle mFreeBlockIndexFile;

        std::unique_ptr<detail::thread_pool> mOpsPool = std::make_unique<detail::thread_pool>();

        std::map<detail::sector_id, std::uint64_t> mFreeSectorPool;
        std::mutex mFreeSectorPoolMutex;
    };


}
