#include "os_filesystem.hpp"

#include <vefs/disappointment.hpp>
#include <vefs/utils/misc.hpp>
#include <vefs/platform/secure_memzero.hpp>

#include "windows-proper.h"

namespace vefs::detail
{
    os_file::~os_file()
    {
        CloseHandle(mFile);
    }

    void os_file::read(rw_dynblob buffer, std::uint64_t readFilePos, std::error_code &ec)
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

            if (!ReadFile(mFile, buffer.data(), portion, &bytesRead, &overlapped) || !bytesRead)
            {
                ec.assign(GetLastError(), std::system_category());
                return;
            }

            splitter.QuadPart += bytesRead;
            buffer = buffer.subspan(bytesRead);
        }
    }

    void os_file::write(ro_dynblob data, std::uint64_t writeFilePos, std::error_code &ec)
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

            if (!WriteFile(mFile, data.data(), portion, &bytesWritten, &overlapped) ||
                !bytesWritten)
            {
                ec.assign(GetLastError(), std::system_category());
                return;
            }

            splitter.QuadPart += bytesWritten;
            data = data.subspan(bytesWritten);
        }
    }

    void os_file::sync(std::error_code &ec)
    {
        if (!FlushFileBuffers(mFile))
        {
            ec.assign(GetLastError(), std::system_category());
            return;
        }
    }

    std::uint64_t os_file::size(std::error_code &ec)
    {
        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(mFile, &fileSize))
        {
            ec.assign(GetLastError(), std::system_category());
            return std::numeric_limits<std::uint64_t>::max();
        }
        return static_cast<std::uint64_t>(fileSize.QuadPart);
    }

    void os_file::resize(std::uint64_t newSize, std::error_code &ec)
    {
        LARGE_INTEGER winSize;
        winSize.QuadPart = static_cast<std::int64_t>(newSize);

        std::lock_guard<std::mutex> sync{mFileMutex};
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

    inline uint32_t derive_access_mode(file_open_mode_bitset mode)
    {
        return GENERIC_READ | (mode % file_open_mode::write) * GENERIC_WRITE;
    }

    inline uint32_t derive_creation_mode(file_open_mode_bitset mode)
    {
        if (mode & file_open_mode::write)
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

    file::ptr os_filesystem::open(const std::filesystem::path &filePath, file_open_mode_bitset mode,
                                  std::error_code &ec)
    {
        auto canonicalPath = weakly_canonical(filePath);
        os_handle file = CreateFileW(canonicalPath.c_str(), derive_access_mode(mode), 0, nullptr,
                                     derive_creation_mode(mode),
                                     FILE_FLAG_POSIX_SEMANTICS | FILE_FLAG_RANDOM_ACCESS, nullptr);

        if (file == INVALID_HANDLE_VALUE)
        {
            ec.assign(GetLastError(), std::system_category());
            return {};
        }

        VEFS_ERROR_EXIT
        {
            CloseHandle(file);
        };

        return std::make_shared<os_file>(mSelf.lock(), file);
    }

    void os_filesystem::remove(const std::filesystem::path &filePath)
    {
        auto canonicalPath = weakly_canonical(filePath);
        if (!DeleteFileW(canonicalPath.c_str()))
        {
            throw error_exception(error(collect_system_error())
                                  << ed::error_code_api_origin("DeleteFileW")
                                  << ed::io_file(std::string{filePath.string()}));
        }
    }
} // namespace vefs::detail
