#pragma once

#include <mutex>

#include <vefs/filesystem.hpp>
#include <vefs/detail/thread_pool.hpp>

#include <boost/predef/os.h>

namespace vefs::detail
{
    class os_filesystem;
#if defined BOOST_OS_WINDOWS_AVAILABLE
    using os_handle = void *;
#else
    using os_handle = int;
#endif

    class os_file
        : public file
    {
    public:
        os_file(std::shared_ptr<os_filesystem> owner, os_handle fileHandle);
        ~os_file();

        void read(blob buffer, std::uint64_t readFilePos, std::error_code& ec) override;
        std::future<void> read_async(blob buffer, std::uint64_t readFilePos,
            async_callback_fn callback) override;
        void write(blob_view data, std::uint64_t writeFilePos, std::error_code& ec) override;
        std::future<void> write_async(blob_view data, std::uint64_t writeFilePos,
            async_callback_fn callback) override;
        void sync(std::error_code& ec) override;
        std::future<void> sync_async(async_callback_fn callback) override;
        std::uint64_t size(std::error_code& ec) override;
        void resize(std::uint64_t newSize, std::error_code& ec) override;
        std::future<void> resize_async(std::uint64_t newSize,
            async_callback_fn callback) override;

    private:
        std::shared_ptr<os_filesystem> mOwner;
        os_handle mFile;
        std::mutex mFileMutex;
    };

    class os_filesystem
        : public filesystem
    {
    public:
        static os_filesystem::ptr create();

        file::ptr open(std::string_view filePath, file_open_mode_bitset mode,
            std::error_code& ec) override;
        void remove(std::string_view filePath) override;

        detail::thread_pool & ops_pool();

    private:
        os_filesystem() = default;

        std::weak_ptr<os_filesystem> mSelf;
    };
}

namespace vefs::detail
{
    inline filesystem::ptr os_filesystem::create()
    {
        std::shared_ptr<os_filesystem> ptr{ new os_filesystem };
        ptr->mSelf = ptr;
        return ptr;
    }

    inline detail::thread_pool & os_filesystem::ops_pool()
    {
        return thread_pool::shared();
    }
}
