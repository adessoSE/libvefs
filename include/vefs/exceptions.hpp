#pragma once

#include <cstdint>

#include <exception>
#include <stdexcept>
#include <boost/exception/all.hpp>

namespace vefs
{
    enum class errinfo_code_tag {};
    using errinfo_code = boost::error_info<errinfo_code_tag, std::error_code>;

    errinfo_code make_system_errinfo_code();

    enum class errinfo_api_function_tag {};
    using errinfo_api_function = boost::error_info<errinfo_api_function_tag, const char *>;


    enum class errinfo_archive_file_tag {};
    using errinfo_archive_file = boost::error_info<errinfo_archive_file_tag, std::string>;

    enum class errinfo_sector_idx_tag {};
    using errinfo_sector_idx = boost::error_info<errinfo_sector_idx_tag, std::uint64_t>;


    class exception
        : public virtual std::exception
        , public virtual boost::exception
    {
    };

    class logic_error
        : public virtual exception
    {
    };

    class crypto_failure
        : public virtual exception
    {
    };

    class archive_corrupted
        : public virtual exception
    {
    };

    class unknown_archive_version
        : public virtual archive_corrupted
    {
    };

    class io_error
        : public virtual exception
    {
    };
}
