#include <vefs/disappointment.hpp>

#include <future>
#include <ios>
#include <mutex>
#include <unordered_map>

#include <boost/config.hpp>
#include <boost/predef.h>

#include <vefs/exceptions.hpp>

#if defined BOOST_OS_WINDOWS_AVAILABLE
#include <status-code/win32_code.hpp>
#include "platform/windows-proper.h"
#elif defined BOOST_OS_LINUX_AVAILABLE || defined BOOST_OS_MACOS_AVAILABLE
#include <cerrno>
#include <status-code/posix_code.hpp>
#endif

namespace vefs
{

auto collect_system_error() -> system_error::system_code
{
#if defined BOOST_OS_WINDOWS_AVAILABLE
    return system_error::win32_code::current();
#elif defined BOOST_OS_LINUX_AVAILABLE || defined BOOST_OS_MACOS_AVAILABLE
    return system_error::posix_code::current();
#endif
}

} // namespace vefs

namespace vefs
{

namespace
{

auto collect_std_system_error() -> std::error_code
{
#if defined BOOST_OS_WINDOWS_AVAILABLE
    return std::error_code{static_cast<int>(GetLastError()),
                           std::system_category()};
#elif defined BOOST_OS_LINUX_AVAILABLE || defined BOOST_OS_MACOS_AVAILABLE
    return std::error_code{errno, std::system_category()};
#endif
}

} // namespace

auto make_system_errinfo_code() -> errinfo_code
{
    return errinfo_code{collect_std_system_error()};
}

} // namespace vefs
