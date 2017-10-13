#include "precompiled.hpp"
#include <vefs/filesystem.hpp>

#include <mutex>
#include <functional>

#include <vefs/thread_pool.hpp>
#include <vefs/utils/misc.hpp>

#include "utf.hpp"
#include "vefs/utils/secure_allocator.hpp"

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace vefs
{
    namespace
    {
        class os_filesystem_impl;
#if defined WIN32
        using os_handle = HANDLE;
#else
        using os_handle = int;
#endif

        class os_file
            : public file
        {
        public:
            os_file(std::shared_ptr<os_filesystem_impl> owner, os_handle fileHandle);
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
            std::shared_ptr<os_filesystem_impl> mOwner;
            os_handle mFile;
            std::mutex mFileMutex;
        };


        os_file::os_file(std::shared_ptr<os_filesystem_impl> owner,
            os_handle fileHandle)
            : mOwner(std::move(owner))
            , mFile(fileHandle)
        {
        }

#if defined WIN32
        os_file::~os_file()
        {
            CloseHandle(mFile);
        }

        void os_file::read(blob buffer, std::uint64_t readFilePos, std::error_code& ec)
        {
            ULARGE_INTEGER splitter;
            splitter.QuadPart = readFilePos;

            OVERLAPPED overlapped;
            utils::secure_data_erase(overlapped);

            while (buffer)
            {
                overlapped.Offset = splitter.LowPart;
                overlapped.OffsetHigh = splitter.HighPart;

                DWORD portion = static_cast<DWORD>(
                    std::min<std::uint64_t>(std::numeric_limits<DWORD>::max(), buffer.size()));
                DWORD bytesRead = 0;

                if (!ReadFile(mFile, buffer.data(), portion, &bytesRead, &overlapped)
                    || !bytesRead)
                {
                    ec.assign(GetLastError(), std::system_category());
                    return;
                }

                splitter.QuadPart += bytesRead;
                buffer.remove_prefix(bytesRead);
            }
        }

        void os_file::write(blob_view data, std::uint64_t writeFilePos,
            std::error_code& ec)
        {
            ULARGE_INTEGER splitter;
            splitter.QuadPart = writeFilePos;

            OVERLAPPED overlapped;
            utils::secure_data_erase(overlapped);

            while (data)
            {
                overlapped.Offset = splitter.LowPart;
                overlapped.OffsetHigh = splitter.HighPart;

                DWORD portion = static_cast<DWORD>(
                    std::min<std::size_t>(std::numeric_limits<DWORD>::max(), data.size()));
                DWORD bytesWritten = 0;

                if (!WriteFile(mFile, data.data(), portion, &bytesWritten, &overlapped)
                    || !bytesWritten)
                {
                    ec.assign(GetLastError(), std::system_category());
                    return;
                }

                splitter.QuadPart += bytesWritten;
                data.remove_prefix(bytesWritten);
            }
        }

        void os_file::sync(std::error_code& ec)
        {
            if (!FlushFileBuffers(mFile))
            {
                ec.assign(GetLastError(), std::system_category());
                return;
            }
        }

        std::uint64_t os_file::size(std::error_code& ec)
        {
            LARGE_INTEGER fileSize;
            if (!GetFileSizeEx(mFile, &fileSize))
            {
                ec.assign(GetLastError(), std::system_category());
                return std::numeric_limits<std::uint64_t>::max();
            }
            return static_cast<std::uint64_t>(fileSize.QuadPart);
        }

        void os_file::resize(std::uint64_t newSize, std::error_code& ec)
        {
            LARGE_INTEGER winSize;
            winSize.QuadPart = static_cast<std::int64_t>(newSize);

            std::lock_guard<std::mutex> sync{ mFileMutex };
            if (!SetFilePointerEx(mFile, winSize, nullptr, FILE_BEGIN))
            {
                ec.assign(GetLastError(), std::system_category());
                return;
            }
            if (!SetEndOfFile(mFile))
            {
                ec.assign(GetLastError(), std::system_category());
                return;
            }
        }
#else
        os_file::~os_file()
        {
        }

        void os_file::read(blob buffer, std::uint64_t readFilePos, std::error_code& ec)
        {
        }

        void os_file::write(blob_view data, std::uint64_t writeFilePos,
            std::error_code& ec)
        {
        }

        void os_file::sync(std::error_code& ec)
        {
        }

        std::uint64_t os_file::size(std::error_code& ec)
        {
        }

        void os_file::resize(std::uint64_t newSize, std::error_code& ec)
        {
        }
#endif


        class os_filesystem_impl
            : public filesystem
        {
        public:
            friend filesystem::ptr vefs::os_filesystem();

            file::ptr open(std::string_view filePath, file_open_mode_bitset mode,
                std::error_code& ec) override;
            void remove(std::string_view filePath) override;

            thread_pool & ops_pool()
            {
                return mOpsPool;
            }

        private:
            thread_pool mOpsPool;
            std::weak_ptr<os_filesystem_impl> mSelf;
        };

#if defined WIN32
        inline uint32_t derive_access_mode(file_open_mode_bitset mode)
        {
            return GENERIC_READ | (mode % file_open_mode::write) * GENERIC_WRITE;
        }

        inline uint32_t derive_creation_mode(file_open_mode_bitset mode)
        {
            if (mode % file_open_mode::write)
            {
                if (mode % (file_open_mode::truncate | file_open_mode::create))
                {
                    return CREATE_ALWAYS;
                }
                if (mode % file_open_mode::truncate)
                {
                    return TRUNCATE_EXISTING;
                }
                if (mode % file_open_mode::create)
                {
                    return OPEN_ALWAYS;
                }
            }
            return OPEN_EXISTING;
        }

        file::ptr os_filesystem_impl::open(std::string_view filePath,
            file_open_mode_bitset mode, std::error_code& ec)
        {
            auto u16FilePath = utf::utf8_to_utf16(filePath);

            os_handle file = CreateFileW(reinterpret_cast<const wchar_t *>(u16FilePath.c_str()),
                derive_access_mode(mode), 0, nullptr, derive_creation_mode(mode),
                FILE_FLAG_POSIX_SEMANTICS | FILE_FLAG_RANDOM_ACCESS, nullptr);

            if (file == INVALID_HANDLE_VALUE)
            {
                ec.assign(GetLastError(), std::system_category());
                return {};
            }

            VEFS_ERROR_EXIT{ CloseHandle(file); };

            return std::make_shared<os_file>(mSelf.lock(), file);
        }

        void os_filesystem_impl::remove(std::string_view filePath)
        {
            auto u16FilePath = utf::utf8_to_utf16(filePath);

            if (!DeleteFileW(reinterpret_cast<const wchar_t *>(u16FilePath.c_str())))
            {
                auto errMsg = std::string{ "Failed to delete the file [" }
                    +std::string{ filePath }
                    +"]";

                throw std::system_error(GetLastError(), std::system_category(),
                    errMsg);
            }
        }
#else
        file::ptr os_filesystem_impl::open(std::string_view filePath,
            file_open_mode_bitset mode, std::error_code& ec)
        {
        }

        void os_filesystem_impl::remove(std::string_view filePath)
        {
        }
#endif

        std::future<void> os_file::read_async(blob buffer, std::uint64_t readFilePos,
                                                   file::async_callback_fn callback)
        {
            return mOwner->ops_pool()
            .exec([this, buffer, readFilePos](async_callback_fn callback)
            {
                std::error_code ec;
                read(buffer, readFilePos, ec);
                callback(std::move(ec));
            }, std::move(callback));
        }

        std::future<void> os_file::write_async(blob_view data, std::uint64_t writeFilePos,
                                                    file::async_callback_fn callback)
        {
            return mOwner->ops_pool()
                .exec([this, data, writeFilePos](async_callback_fn callback)
            {
                std::error_code ec;
                write(data, writeFilePos, ec);
                callback(std::move(ec));
            }, std::move(callback));
        }

        std::future<void> os_file::sync_async(file::async_callback_fn callback)
        {
            return mOwner->ops_pool()
                .exec([this](async_callback_fn callback)
            {
                std::error_code ec;
                sync(ec);
                callback(std::move(ec));
            }, std::move(callback));
        }

        std::future<void> os_file::resize_async(std::uint64_t newSize,
                                                     file::async_callback_fn callback)
        {
            return mOwner->ops_pool()
                .exec([this, newSize](async_callback_fn callback)
            {
                std::error_code ec;
                resize(newSize, ec);
                callback(std::move(ec));
            }, std::move(callback));
        }
    }

    filesystem::ptr os_filesystem()
    {
        auto impl = std::make_shared<os_filesystem_impl>();
        impl->mSelf = impl;

        return impl;
    }
}
