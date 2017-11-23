#include "precompiled.hpp"
#include <vefs/filesystem.hpp>

#include <functional>

#include <vefs/utils/misc.hpp>

namespace vefs
{
    void file::read(blob buffer, std::uint64_t readFilePos)
    {
        using namespace utils;
        error_code_scope(std::mem_fn<void(blob, std::uint64_t, std::error_code &)>(&file::read),
            this, buffer, readFilePos);
    }

    void file::write(blob_view data, std::uint64_t writeFilePos)
    {
        using namespace utils;
        error_code_scope(std::mem_fn<void(blob_view, std::uint64_t, std::error_code &)>(&file::write),
            this, data, writeFilePos);
    }

    void file::sync()
    {
        using namespace utils;
        error_code_scope(std::mem_fn<void(std::error_code &)>(&file::sync), this);
    }

    std::uint64_t file::size()
    {
        using namespace utils;
        return error_code_scope(std::mem_fn<std::uint64_t(std::error_code &)>(&file::size), this);
    }

    void file::resize(std::uint64_t newSize)
    {
        using namespace utils;
        error_code_scope(std::mem_fn<void(std::uint64_t, std::error_code &)>(&file::resize),
            this, newSize);
    }

    file::ptr filesystem::open(std::string_view filePath, file_open_mode_bitset mode)
    {
        using namespace utils;
        return error_code_scope(std::mem_fn<file::ptr(std::string_view, file_open_mode_bitset, std::error_code &)>(&filesystem::open),
            this, filePath, mode);
    }
}
