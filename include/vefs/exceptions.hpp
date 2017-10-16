#pragma once

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


    class exception
        : public virtual std::exception
        , public virtual boost::exception
    {
    };

    class crypto_failure
        : public virtual exception
    {
    };
}
