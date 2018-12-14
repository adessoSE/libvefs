#include "precompiled.hpp"
#include "sysrandom.hpp"

#include <stdexcept>
#include <type_traits>

#include <boost/predef.h>

#include <vefs/exceptions.hpp>
#include <vefs/utils/misc.hpp>

#if defined BOOST_OS_WINDOWS_AVAILABLE
#include <vefs/utils/windows-proper.h>

#define RtlGenRandom SystemFunction036
extern "C" BOOLEAN NTAPI RtlGenRandom(PVOID RandomBuffer, ULONG RandomBufferLength);

vefs::result<void> vefs::detail::random_bytes(blob buffer) noexcept
{
    using namespace vefs;
    using namespace std::string_view_literals;

    if (!buffer)
    {
        return error{ errc::invalid_argument }
            << ed::error_code_api_origin{ "random_bytes"sv };
    }
    do
    {
        using c_type = std::common_type_t<ULONG, std::size_t>;
        constexpr auto max_portion = std::numeric_limits<ULONG>::max();
        const auto portion = static_cast<ULONG>(std::min<c_type>(max_portion, buffer.size()));

        if (!RtlGenRandom(buffer.data(), portion))
        {
            return error{ collect_system_error() }
                << ed::error_code_api_origin{ "SystemFunction036"sv };
        }
        buffer.remove_prefix(portion);
    }
    while (buffer);

    return outcome::success();
}

#elif defined BOOST_OS_UNIX_AVAILABLE

#if __has_include(<sys/random.h>)
#include <sys/random.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#if !__has_include(<sys/random.h>)

vefs::result<void> vefs::detail::random_bytes(blob buffer) noexcept
{
    using namespace vefs;
    using namespace std::string_view_literals;

    if (!buffer)
    {
        return error{ errc::invalid_argument }
            << ed::error_code_api_origin{ "random_bytes"sv };
    }

    int urandom = open("/dev/urandom", O_RDONLY);
    if (urandom == -1)
    {
        return error{ collect_system_error() }
            << ed::error_code_origin_tag{ "open(\"/dev/urandom\")"sv });
    }
    VEFS_SCOPE_EXIT{
        close(urandom);
    };

    while (buffer)
    {
        constexpr size_t max_portion = static_cast<size_t>(std::numeric_limits<ssize_t>::max());
        const auto portion = std::min(max_portion, buffer.size());

        ssize_t tmp = read(urandom, buffer.data(), portion);
        if (tmp == -1)
        {
            return error{ collect_system_error() }
            << ed::error_code_origin_tag{ "read(\"/dev/urandom\")"sv });
        }
        if (tmp == 0)
        {
            return errc::bad
                << ed::error_code_origin_tag{ "read(\"/dev/urandom\")"sv });
        }
        buffer.remove_prefix(static_cast<size_t>(tmp));
    }
    return outcome::success();
}

#else

result<void> vefs::detail::random_bytes(blob buffer) noexcept
{
    using namespace vefs;
    using namespace std::string_view_literals;

    if (!buffer)
    {
        return error{ errc::invalid_argument }
            << ed::error_code_api_origin{ "random_bytes"sv };
    }

    using ptr = std::add_pointer_t<std::void_t<tag>>;
    while (buffer)
    {
        constexpr size_t max_portion = static_cast<size_t>(33554431);
        const auto portion = std::min(max_portion, buffer.size());

        ssize_t tmp = getrandom(static_cast<ptr>(buffer.data()), portion, 0);
        if (tmp == -1)
        {
            return error{ collect_system_error() }
                << ed::error_code_origin_tag{ "getrandom" };
        }
        if (tmp == 0)
        {
            return errc::bad
                << ed::error_code_origin_tag{ "getrandom" };
        }
        buffer.remove_prefix(static_cast<size_t>(tmp));
    }
    return outcome::success();
}

#endif

#else
#error "platform::random is not implemented on your operating system"
#endif

