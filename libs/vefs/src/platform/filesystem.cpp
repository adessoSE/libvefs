#include <vefs/platform/filesystem.hpp>

#include <functional>

#include <vefs/disappointment.hpp>

namespace vefs
{
    void file::read(rw_dynblob buffer, std::uint64_t readFilePos)
    {
        std::error_code ec;
        read(buffer, readFilePos, ec);
        if (ec)
        {
            throw error_exception(error(ec));
        }
    }

    void file::write(ro_dynblob data, std::uint64_t writeFilePos)
    {
        std::error_code ec;
        write(data, writeFilePos, ec);
        if (ec)
        {
            throw error_exception(error(ec));
        }
    }

    void file::sync()
    {
        std::error_code ec;
        sync(ec);
        if (ec)
        {
            throw error_exception(error(ec));
        }
    }

    std::uint64_t file::size()
    {
        std::error_code ec;
        auto result = size(ec);
        if (ec)
        {
            throw error_exception(error(ec));
        }
        return result;
    }

    void file::resize(std::uint64_t newSize)
    {
        std::error_code ec;
        resize(newSize, ec);
        if (ec)
        {
            throw error_exception(error(ec));
        }
    }

    file::ptr filesystem::open(const std::filesystem::path &filePath, file_open_mode_bitset mode)
    {
        std::error_code ec;
        auto result = open(filePath, mode, ec);
        if (ec)
        {
            throw error_exception(error(ec));
        }
        return result;
    }
} // namespace vefs
