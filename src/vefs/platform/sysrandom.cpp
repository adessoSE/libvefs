#include "sysrandom.hpp"

#include <stdexcept>
#include <type_traits>

#include <boost/predef.h>

#include <vefs/exceptions.hpp>
#include <vefs/utils/misc.hpp>

#if defined BOOST_OS_WINDOWS_AVAILABLE
#include "windows-proper.h"

#define RtlGenRandom SystemFunction036
extern "C" BOOLEAN NTAPI RtlGenRandom(PVOID RandomBuffer,
                                      ULONG RandomBufferLength);

/**
 * Windows implementation of a cryptographically safe random bytes generator.
 * Uses windows builtin-function.
 */
vefs::result<void> vefs::detail::random_bytes(rw_dynblob buffer) noexcept
{
    using namespace vefs;
    using namespace std::string_view_literals;

    if (buffer.empty())
    {
        return errc::invalid_argument
               << ed::error_code_api_origin{"random_bytes"sv};
    }
    do
    {
        using c_type = std::common_type_t<ULONG, std::size_t>;
        constexpr auto max_portion = std::numeric_limits<ULONG>::max();
        auto const portion = static_cast<ULONG>(
                std::min<c_type>(max_portion, buffer.size()));

        if (!RtlGenRandom(buffer.data(), portion))
        {
            return collect_system_error()
                   << ed::error_code_api_origin{"SystemFunction036"sv};
        }
        buffer = buffer.subspan(portion);
    }
    while (!buffer.empty());

    return outcome::success();
}

#elif defined BOOST_OS_UNIX_AVAILABLE

#if __has_include(<sys/random.h>)
#include <sys/random.h>
#endif

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if !__has_include(<sys/random.h>)

vefs::result<void> vefs::detail::random_bytes(rw_dynblob buffer) noexcept
{
    using namespace vefs;
    using namespace std::string_view_literals;

    if (buffer.empty())
    {
        return errc::invalid_argument
               << ed::error_code_api_origin{"random_bytes"sv};
    }

    int urandom = open("/dev/urandom", O_RDONLY);
    if (urandom == -1)
    {
        return collect_system_error()
               << ed::error_code_api_origin{"open(\"/dev/urandom\")"sv};
    }
    VEFS_SCOPE_EXIT
    {
        close(urandom);
    };

    while (!buffer.empty())
    {
        constexpr size_t max_portion
                = static_cast<size_t>(std::numeric_limits<ssize_t>::max());
        auto const portion = std::min(max_portion, buffer.size());

        ssize_t tmp = read(urandom, buffer.data(), portion);
        if (tmp == -1)
        {
            return collect_system_error()
                   << ed::error_code_api_origin{"read(\"/dev/urandom\")"sv};
        }
        if (tmp == 0)
        {
            return archive_errc::bad
                   << ed::error_code_api_origin{"read(\"/dev/urandom\")"sv};
        }
        buffer = buffer.subspan(static_cast<size_t>(tmp));
    }
    return outcome::success();
}

#else

vefs::result<void> vefs::detail::random_bytes(rw_dynblob buffer) noexcept
{
    using namespace vefs;
    using namespace std::string_view_literals;

    if (buffer.empty())
    {
        return errc::invalid_argument
               << ed::error_code_api_origin{"random_bytes"sv};
    }

    while (!buffer.empty())
    {
        constexpr size_t max_portion = static_cast<size_t>(33'554'431);
        auto const portion = std::min(max_portion, buffer.size());

        ssize_t tmp = getrandom(static_cast<void *>(buffer.data()), portion, 0);
        if (tmp == -1)
        {
            return collect_system_error()
                   << ed::error_code_api_origin{"getrandom"sv};
        }
        if (tmp == 0)
        {
            return archive_errc::bad
                   << ed::error_code_api_origin{"getrandom"sv};
        }
        buffer = buffer.subspan(static_cast<size_t>(tmp));
    }
    return outcome::success();
}

#endif

#else
#error "random_bytes() is not implemented on your operating system"
#endif
