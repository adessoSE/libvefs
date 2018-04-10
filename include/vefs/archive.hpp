#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string_view>

#include <vefs/archive_fwd.hpp>
#include <vefs/blob.hpp>
#include <vefs/filesystem.hpp>
#include <vefs/utils/ref_ptr.hpp>
#include <vefs/utils/async_error_info.hpp>


namespace vefs
{
    struct file_query_result
    {
        file_open_mode_bitset allowed_flags;
        std::size_t size;
    };

    class archive
    {
        class file_walker;
        class writer_task;
    public:
        enum class create_tag {};
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
        void sync_async(std::function<void(utils::async_error_info)> cb);

        file_handle open(const std::string_view filePath, const file_open_mode_bitset mode);
        std::optional<file_query_result> query(const std::string_view filePath);
        void erase(std::string_view filePath);
        void read(file_handle handle, blob buffer, std::uint64_t readFilePos);
        void write(file_handle handle, blob_view data, std::uint64_t writeFilePos);
        void resize(file_handle handle, std::uint64_t size);
        std::uint64_t size_of(file_handle handle);
        void sync(file_handle handle);


        void erase_async(std::string filePath,
            std::function<void(utils::async_error_info)> cb);
        void read_async(file_handle handle, blob buffer, std::uint64_t readFilePos,
            std::function<void(utils::async_error_info)> cb);
        void write_async(file_handle handle, blob_view data, std::uint64_t writeFilePos,
            std::function<void(utils::async_error_info)> cb);
        void resize_async(file_handle handle, std::uint64_t size,
            std::function<void(utils::async_error_info)> cb);
        void size_of_async(file_handle handle,
            std::function<void(std::uint64_t, utils::async_error_info)> cb);
        void sync_async(file_handle handle,
            std::function<void(utils::async_error_info)> cb);

    private:
        archive();

        void read_archive_index();
        void write_archive_index();

        std::unique_ptr<detail::raw_archive> mArchive;

        std::shared_ptr<index_file> mArchiveIndexFile;
        std::shared_ptr<free_block_list_file> mFreeBlockIndexFile;

        std::unique_ptr<detail::thread_pool> mOpsPool;
    };
}

namespace vefs
{
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
