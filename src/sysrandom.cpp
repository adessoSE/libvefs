#include "precompiled.hpp"
#include "sysrandom.hpp"

#include <stdexcept>
#include <type_traits>

#include <boost/predef.h>

#include <vefs/exceptions.hpp>
#include <vefs/utils/misc.hpp>

#if defined BOOST_OS_WINDOWS_AVAILABLE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#define RtlGenRandom SystemFunction036
extern "C" BOOLEAN NTAPI RtlGenRandom(PVOID RandomBuffer, ULONG RandomBufferLength);

void vefs::detail::random_bytes(blob buffer)
{
    if (!buffer)
    {
        BOOST_THROW_EXCEPTION(invalid_argument{}
            << errinfo_param_name{ "buffer" }
            << errinfo_param_misuse_description{ "n" }
        );
    }
    do
    {
        using c_type = std::common_type_t<ULONG, std::size_t>;
        constexpr auto max_portion = std::numeric_limits<ULONG>::max();
        const auto portion = static_cast<ULONG>(std::min<c_type>(max_portion, buffer.size()));

        if (!RtlGenRandom(buffer.data(), portion))
        {
            BOOST_THROW_EXCEPTION(vefs::crypto_failure{}
                << vefs::make_system_errinfo_code()
                << vefs::errinfo_api_function{ "SystemFunction036" });
        }
        buffer.remove_prefix(portion);
    }
    while (buffer);
}

#elif defined BOOST_OS_UNIX_AVAILABLE

#if __has_include(<sys/random.h>)
#include <sys/random.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

template <typename = void>
struct has_getrandom
    : std::false_type {};

template <>
struct has_getrandom<std::void_t<decltype(getrandom(nullptr, 0, 0))>>
    : std::true_type {};

constexpr bool has_getrandom_v = has_getrandom::value;

namespace
{
    void random_bytes_device_impl(blob buffer)
    {
        int urandom = open("/dev/urandom", O_RDONLY);
        if (urandom == -1)
        {
            std::error_code ec{ errno, std::system_category() };
            BOOST_THROW_EXCEPTION(vefs::crypto_failure{}
                << vefs::errinfo_code{ ec }
                << vefs::errinfo_api_function{ "open" });
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
                std::error_code ec{ errno, std::system_category() };
                BOOST_THROW_EXCEPTION(vefs::crypto_failure{}
                    << vefs::errinfo_code{ ec }
                    << vefs::errinfo_api_function{ "read" });
            }
            if (tmp == 0)
            {
                BOOST_THROW_EXCEPTION(vefs::crypto_failure{}
                    << vefs::errinfo_api_function{ "read" });
            }
            buffer.remove_prefix(static_cast<size_t>(tmp));
        }
    }

    template <typename tag>
    void random_bytes_impl(blob buffer)
    {
        if constexpr (tag::value)
        {
            using ptr = std::add_pointer_t<std::void_t<tag>>;
            while (buffer)
            {
                constexpr size_t max_portion = static_cast<size_t>(33554431);
                const auto portion = std::min(max_portion, buffer.size());

                ssize_t tmp = getrandom(static_cast<ptr>(buffer.data()), portion, 0);
                if (tmp == -1)
                {
                    std::error_code ec{ errno, std::system_category() };
                    BOOST_THROW_EXCEPTION(vefs::crypto_failure{}
                        << vefs::errinfo_code{ ec }
                        << vefs::errinfo_api_function{ "getrandom" });
                }
                if (tmp == 0)
                {
                    BOOST_THROW_EXCEPTION(vefs::crypto_failure{}
                        << vefs::errinfo_api_function{ "getrandom" });
                }
                buffer.remove_prefix(static_cast<size_t>(tmp));
            }
        }
        else
        {
            random_bytes_device_impl(buffer);
        }
    }
}

void vefs::detail::random_bytes(blob buffer)
{
    if (!buffer)
    {
        BOOST_THROW_EXCEPTION(invalid_argument{}
            << errinfo_param_name{ "buffer" }
            << errinfo_param_misuse_description{ "n" }
        );
    }
    random_bytes_impl<has_getrandom<>>(buffer);
}

#else
#error "platform::random is not implemented on your operating system"
#endif

