#include "os_filesystem.hpp"

#include <vefs/utils/misc.hpp>
#include <vefs/utils/secure_ops.hpp>

#error "the filesystem abstraction is not implemented for linux"

namespace vefs::detail
{
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


    file::ptr os_filesystem_impl::open(std::string_view filePath,
        file_open_mode_bitset mode, std::error_code& ec)
    {
    }

    void os_filesystem_impl::remove(std::string_view filePath)
    {
    }
}
