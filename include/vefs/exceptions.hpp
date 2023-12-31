#pragma once

#include <cstdint>

#include <exception>
#include <stdexcept>
#include <type_traits>

#include <boost/predef/compiler.h>

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(push, 3)
#pragma warning(disable : 6246)
#endif
#if defined(BOOST_COMP_CLANG_AVAILABLE)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpotentially-evaluated-expression"
#pragma clang diagnostic ignored "-Wdelete-non-abstract-non-virtual-dtor"
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif

#include <boost/exception/all.hpp>
#include <boost/throw_exception.hpp>

#if defined(BOOST_COMP_CLANG_AVAILABLE)
#pragma clang diagnostic pop
#endif
#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(pop)
#endif

namespace vefs
{
enum class errinfo_code_tag
{
};
using errinfo_code = boost::error_info<errinfo_code_tag, std::error_code>;

errinfo_code make_system_errinfo_code();

enum class errinfo_api_function_tag
{
};
using errinfo_api_function
        = boost::error_info<errinfo_api_function_tag, char const *>;

enum class errinfo_param_name_tag
{
};
using errinfo_param_name
        = boost::error_info<errinfo_param_name_tag, char const *>;

enum class errinfo_param_misuse_description_tag
{
};
using errinfo_param_misuse_description
        = boost::error_info<errinfo_param_misuse_description_tag, std::string>;

class exception
    : public virtual std::exception
    , public virtual boost::exception
{
};

class logic_error : public virtual exception
{
};

class invalid_argument : public virtual logic_error
{
};
} // namespace vefs
