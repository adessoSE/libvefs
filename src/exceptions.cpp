#include "precompiled.hpp"
#include <vefs/exceptions.hpp>

#include <boost/predef.h>

#if defined BOOST_OS_WINDOWS_AVAILABLE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined BOOST_OS_LINUX_AVAILABLE || defined BOOST_OS_MACOS_AVAILABLE
#include <cerrno>
#endif

namespace vefs
{
    errinfo_code make_system_errinfo_code()
    {
#if defined BOOST_OS_WINDOWS_AVAILABLE
        std::error_code ec{ static_cast<int>(GetLastError()), std::system_category() };
#elif defined BOOST_OS_LINUX_AVAILABLE || defined BOOST_OS_MACOS_AVAILABLE
        std::error_code ec{ errno, std::system_category() };
#endif
        return errinfo_code{ ec };
    }
}
