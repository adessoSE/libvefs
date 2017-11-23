#pragma once

#include <cstdint>

#include <exception>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <boost/exception/all.hpp>

#include <vefs/detail/sector_id.hpp>

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
    using errinfo_sector_idx = boost::error_info<errinfo_sector_idx_tag, detail::sector_id>;


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

    class sector_reference_out_of_range
        : public virtual archive_corrupted
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


    enum class vefs_error_code
    {

    };
    const std::error_category & vefs_category();
    inline std::error_code make_error_code(vefs_error_code code)
    {
        return { static_cast<int>(code), vefs_category() };
    }
}

namespace std
{
    template <>
    struct is_error_code_enum<vefs::vefs_error_code>
        : std::true_type
    {
    };
}
