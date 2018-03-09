#include "precompiled.hpp"
#include <vefs/filesystem.hpp>

#include <functional>

#include <vefs/exceptions.hpp>

namespace vefs
{
    void file::read(blob buffer, std::uint64_t readFilePos)
    {
        std::error_code ec;
        read(buffer, readFilePos, ec);
        if (ec)
        {
            BOOST_THROW_EXCEPTION(io_error{}
                << errinfo_code{ ec }
            );
        }
    }

    void file::write(blob_view data, std::uint64_t writeFilePos)
    {
        std::error_code ec;
        write(data, writeFilePos, ec);
        if (ec)
        {
            BOOST_THROW_EXCEPTION(io_error{}
                << errinfo_code{ ec }
            );
        }
    }

    void file::sync()
    {
        std::error_code ec;
        sync(ec);
        if (ec)
        {
            BOOST_THROW_EXCEPTION(io_error{}
                << errinfo_code{ ec }
            );
        }
    }

    std::uint64_t file::size()
    {
        std::error_code ec;
        auto result = size(ec);
        if (ec)
        {
            BOOST_THROW_EXCEPTION(io_error{}
                << errinfo_code{ ec }
            );
        }
        return result;
    }

    void file::resize(std::uint64_t newSize)
    {
        std::error_code ec;
        resize(newSize, ec);
        if (ec)
        {
            BOOST_THROW_EXCEPTION(io_error{}
                << errinfo_code{ ec }
            );
        }
    }

    file::ptr filesystem::open(std::string_view filePath, file_open_mode_bitset mode)
    {
        std::error_code ec;
        auto result = open(filePath, mode, ec);
        if (ec)
        {
            BOOST_THROW_EXCEPTION(io_error{}
                << errinfo_code{ ec }
            );
        }
        return result;
    }
}
