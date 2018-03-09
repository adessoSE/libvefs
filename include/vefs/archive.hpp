#pragma once

#include <map>
#include <mutex>
#include <atomic>
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
#include <vefs/detail/thread_pool.hpp>
#include <vefs/utils/ref_ptr.hpp>
#include <vefs/utils/unordered_map_mt.hpp>


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

    private:
        class internal_file;
        class index_file;
        class free_block_list_file;

        class file_lookup;
        using file_lookup_ptr = utils::ref_ptr<file_lookup>;

    public:
        class file_handle final
        {
            friend class file_lookup;
            friend bool operator==(const file_handle &, const file_handle &);

            inline explicit file_handle(file_lookup &data);

        public:
            inline file_handle() noexcept;
            inline file_handle(const file_handle &other) noexcept;
            inline file_handle(file_handle &&other) noexcept;
            inline file_handle(nullptr_t);
            inline ~file_handle();

            inline file_handle & operator=(const file_handle &other) noexcept;
            inline file_handle & operator=(file_handle &&other) noexcept;
            inline file_handle & operator=(std::nullptr_t);

            inline explicit operator bool() const;

        private:
            void add_reference();
            void release();

            file_lookup *mData;
        };

        archive(filesystem::ptr fs, std::string_view archivePath,
            crypto::crypto_provider *cryptoProvider, blob_view userPRK);
        archive(filesystem::ptr fs, std::string_view archivePath,
            crypto::crypto_provider *cryptoProvider, blob_view userPRK, create_tag);
        ~archive();

        void sync();

        file_handle open(const std::string_view filePath, const file_open_mode_bitset mode);
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

        inline void mark_dirty();
        inline void mark_clean();
        inline bool is_dirty();

        std::unique_ptr<detail::raw_archive> mArchive;

        utils::unordered_string_map_mt<detail::file_id> mIndex;
        utils::unordered_map_mt<detail::file_id, file_lookup_ptr> mFileHandles;

        std::shared_ptr<index_file> mArchiveIndexFile;
        std::shared_ptr<free_block_list_file> mFreeBlockIndexFile;

        std::unique_ptr<detail::thread_pool> mOpsPool;

        std::atomic<bool> mDirty;
    };
}

namespace vefs
{
    inline void vefs::archive::mark_dirty()
    {
        mDirty.store(true, std::memory_order_release);
    }
    inline void vefs::archive::mark_clean()
    {
        mDirty.store(false, std::memory_order_release);
    }
    inline bool vefs::archive::is_dirty()
    {
        return mDirty.load(std::memory_order_acquire);
    }

    inline archive::file_handle::file_handle() noexcept
        : mData{ nullptr }
    {
    }
    inline archive::file_handle::file_handle(std::nullptr_t)
        : file_handle{}
    {
    }
    inline archive::file_handle::file_handle(file_lookup &data)
        : mData{ &data }
    {
    }
    inline archive::file_handle::file_handle(const file_handle &other) noexcept
        : mData{ other.mData }
    {
        if (mData)
        {
            add_reference();
        }
    }
    inline archive::file_handle::file_handle(file_handle &&other) noexcept
        : mData{ other.mData }
    {
        other.mData = nullptr;
    }
    inline archive::file_handle::~file_handle()
    {
        if (mData)
        {
            release();
        }
    }

    inline archive::file_handle & archive::file_handle::operator=(std::nullptr_t)
    {
        if (mData)
        {
            release();
            mData = nullptr;
        }

        return *this;
    }
    inline archive::file_handle & archive::file_handle::operator=(const file_handle &other) noexcept
    {
        if (mData)
        {
            release();
        }
        mData = other.mData;
        if (mData)
        {
            add_reference();
        }

        return *this;
    }
    inline archive::file_handle & archive::file_handle::operator=(file_handle &&other) noexcept
    {
        if (mData)
        {
            release();
        }
        mData = other.mData;
        other.mData = nullptr;

        return *this;
    }

    inline archive::file_handle::operator bool() const
    {
        return mData != nullptr;
    }

    inline bool operator==(const archive::file_handle &lhs, const archive::file_handle &rhs)
    {
        return lhs.mData == rhs.mData;
    }
    inline bool operator!=(const archive::file_handle &lhs, const archive::file_handle &rhs)
    {
        return !(lhs == rhs);
    }
}
