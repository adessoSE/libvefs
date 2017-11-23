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

    namespace
    {
        class vefs_error_category
            : public std::error_category
        {
        public:
#if !defined BOOST_COMP_MSVC_AVAILABLE
            constexpr
#endif
                vefs_error_category() = default;


            // Inherited via error_category
            virtual const char * name() const noexcept override
            {
                return "vefs";
            }

            virtual std::string message(int errval) const override
            {
                switch (vefs_error_code{ errval })
                {
                default:
                    return std::string{ "unknown mvefs error code: #" } +std::to_string(errval);
                }
            }

        };

#if defined BOOST_COMP_MSVC_AVAILABLE
        const
#else
        constexpr
#endif
        vefs_error_category gVefsErrorCategory;
    }

    const std::error_category & vefs_category()
    {
        return gVefsErrorCategory;
    }
}
