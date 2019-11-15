#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string_view>

#include <vefs/archive_fwd.hpp>
#include <vefs/span.hpp>
#include <vefs/platform/thread_pool.hpp>
#include <vefs/disappointment.hpp>
#include <vefs/platform/filesystem.hpp>
#include <vefs/utils/ref_ptr.hpp>

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
        enum class create_tag
        {
        };
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

            inline file_handle &operator=(const file_handle &other) noexcept;
            inline file_handle &operator=(file_handle &&other) noexcept;
            inline file_handle &operator=(std::nullptr_t);

            inline explicit operator bool() const;

            inline auto value() const noexcept -> file_lookup *;

            friend file *deref(const file_handle &) noexcept;

        private:
            void add_reference();
            void release();

            file_lookup *mData;
        };

        static auto open(filesystem::ptr fs, const std::filesystem::path &archivePath,
                         crypto::crypto_provider *cryptoProvider,
                         ro_blob<32> userPRK, file_open_mode_bitset openMode) -> result<std::unique_ptr<archive>>;
        ~archive();

        auto sync() -> result<void>;
        void sync_async(std::function<void(op_outcome<void>)> cb);

        auto open(const std::string_view filePath, const file_open_mode_bitset mode) -> result<file_handle>;
        auto query(const std::string_view filePath) -> result<file_query_result>;
        auto erase(std::string_view filePath) -> result<void>;
        auto read(file_handle handle, rw_dynblob buffer, std::uint64_t readFilePos) -> result<void>;
        auto write(file_handle handle, ro_dynblob data, std::uint64_t writeFilePos) -> result<void>;
        auto resize(file_handle handle, std::uint64_t size) -> result<void>;
        auto size_of(file_handle handle) -> result<std::uint64_t>;
        auto sync(file_handle handle) -> result<void>;

        void erase_async(std::string filePath, std::function<void(op_outcome<void>)> cb);
        void read_async(file_handle handle, rw_dynblob buffer, std::uint64_t readFilePos,
                        std::function<void(op_outcome<void>)> cb);
        void write_async(file_handle handle, ro_dynblob data, std::uint64_t writeFilePos,
                         std::function<void(op_outcome<void>)> cb);
        void resize_async(file_handle handle, std::uint64_t size, std::function<void(op_outcome<void>)> cb);
        void size_of_async(file_handle handle, std::function<void(op_outcome<std::uint64_t>)> cb);
        void sync_async(file_handle handle, std::function<void(op_outcome<void>)> cb);

    private:
        archive();
        archive(std::unique_ptr<detail::sector_device> primitives);
        detail::thread_pool &ops_pool();

        std::unique_ptr<detail::sector_device> mArchive;

        std::shared_ptr<index_file> mArchiveIndexFile;
        std::shared_ptr<free_block_list_file> mFreeBlockIndexFile;

        detail::pooled_work_tracker mWorkTracker;
    };
} // namespace vefs

namespace vefs
{
    inline detail::thread_pool &vefs::archive::ops_pool()
    {
        return mWorkTracker;
    }

    inline archive::file_handle::file_handle() noexcept
        : mData{nullptr}
    {
    }
    inline archive::file_handle::file_handle(std::nullptr_t)
        : file_handle{}
    {
    }
    inline archive::file_handle::file_handle(file_lookup &data)
        : mData{&data}
    {
    }
    inline archive::file_handle::file_handle(const file_handle &other) noexcept
        : mData{other.mData}
    {
        if (mData)
        {
            add_reference();
        }
    }
    inline archive::file_handle::file_handle(file_handle &&other) noexcept
        : mData{other.mData}
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

    inline archive::file_handle &archive::file_handle::operator=(std::nullptr_t)
    {
        if (mData)
        {
            release();
            mData = nullptr;
        }

        return *this;
    }
    inline archive::file_handle &archive::file_handle::operator=(const file_handle &other) noexcept
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
    inline archive::file_handle &archive::file_handle::operator=(file_handle &&other) noexcept
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

    inline auto archive::file_handle::value() const noexcept -> file_lookup *
    {
        return mData;
    }

    inline bool operator==(const archive::file_handle &lhs, const archive::file_handle &rhs)
    {
        return lhs.mData == rhs.mData;
    }
    inline bool operator!=(const archive::file_handle &lhs, const archive::file_handle &rhs)
    {
        return !(lhs == rhs);
    }
} // namespace vefs
