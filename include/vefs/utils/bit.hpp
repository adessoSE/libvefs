#pragma once

#include <cstddef>

#include <limits>
#include <type_traits>

#include <boost/predef.h>

#if __cplusplus > 201'703L && __has_include(<version>)
#include <version>
#endif

#if __cpp_lib_bitops >= 201'907L
#include <bit>

namespace vefs::utils
{
using std::countl_one;
using std::countl_zero;
using std::countr_one;
using std::countr_zero;
} // namespace vefs::utils

#else

namespace vefs::utils
{
template <class U1, class U2>
inline constexpr int unsigned_digit_distance_v
        = std::numeric_limits<U1>::digits - std::numeric_limits<U2>::digits;

template <class T>
inline int countl_zero(T x) noexcept
{
    static_assert(std::is_integral_v<T>);
    static_assert(std::is_unsigned_v<T>);
    static_assert(sizeof(T) <= sizeof(unsigned long long));

    using limits = std::numeric_limits<T>;

    if (x == 0)
    {
        return limits::digits;
    }

#if defined BOOST_COMP_MSVC_AVAILABLE

    unsigned long result;
    if constexpr (sizeof(T) <= sizeof(unsigned long))
    {
        _BitScanReverse(&result, static_cast<unsigned long>(x));
        result -= unsigned_digit_distance_v<unsigned long, T>;
    }
    else if constexpr (sizeof(T) <= sizeof(unsigned long long))
    {
#if defined(_M_ARM64) || defined(_M_AMD64)

        _BitScanReverse64(&result, static_cast<unsigned long long>(x));
        result -= unsigned_digit_distance_v<unsigned long long, T>;

#else
        static_assert(sizeof(unsigned long) * 2 == sizeof(unsigned long long));

        using ulong_limits = std::numeric_limits<unsigned long>;
        if (_BitScanReverse(&result, static_cast<unsigned long>(
                                             x >> ulong_limits::digits)))
        {
            result -= unsigned_digit_distance_v<unsigned long long, T>;
        }
        else
        {
            _BitScanReverse(&result, static_cast<unsigned long>(x));
            result += ulong_limits::digits;
            result -= unsigned_digit_distance_v<unsigned long long, T>;
        }
#endif
    }
    return static_cast<int>(result);

#elif defined(BOOST_COMP_GNUC_AVAILABLE) || defined(BOOST_COMP_CLANG_AVAILABLE)

    int result;
    if constexpr (sizeof(T) <= sizeof(unsigned int))
    {
        result = __builtin_clz(static_cast<unsigned int>(x));
        result -= unsigned_digit_distance_v<unsigned int, T>;
    }
    else if constexpr (sizeof(T) <= sizeof(unsigned long))
    {
        result = __builtin_clzl(static_cast<unsigned long>(x));
        result -= unsigned_digit_distance_v<unsigned long, T>;
    }
    else if constexpr (sizeof(T) <= sizeof(unsigned long long))
    {
        result = __builtin_clzll(static_cast<unsigned long long>(x));
        result -= unsigned_digit_distance_v<unsigned long long, T>;
    }
    return result;

#else
#error countl_zero has not been ported to this compiler
#endif
}

template <class T>
inline int countr_zero(T x) noexcept
{
    static_assert(std::is_integral_v<T>);
    static_assert(std::is_unsigned_v<T>);
    static_assert(sizeof(T) <= sizeof(unsigned long long));

    using limits = std::numeric_limits<T>;

    if (x == 0)
    {
        return limits::digits;
    }

#if defined BOOST_COMP_MSVC_AVAILABLE

    unsigned long result;
    if constexpr (sizeof(T) <= sizeof(unsigned long))
    {
        _BitScanForward(&result, static_cast<unsigned long>(x));
        return static_cast<int>(result);
    }
    else if constexpr (sizeof(T) <= sizeof(unsigned long long))
    {

#if defined(_M_ARM64) || defined(_M_AMD64)

        _BitScanForward64(&result, static_cast<unsigned long long>(x));
        return static_cast<int>(result);

#else

        static_assert(sizeof(unsigned long) * 2 == sizeof(unsigned long long));

        if (_BitScanForward(&result, static_cast<unsigned long>(x)))
        {
            return static_cast<int>(result);
        }
        else
        {
            _BitScanForward(
                    &result,
                    static_cast<unsigned long>(
                            x >> std::numeric_limits<unsigned long>::digits));
            return static_cast<int>(result);
        }
#endif
    }

#elif defined BOOST_COMP_GNUC_AVAILABLE || defined BOOST_COMP_CLANG_AVAILABLE

    int result;
    if constexpr (sizeof(T) <= sizeof(unsigned int))
    {
        result = __builtin_ffs(static_cast<unsigned int>(x));
    }
    else if constexpr (sizeof(T) <= sizeof(unsigned long))
    {
        result = __builtin_ffsl(static_cast<unsigned long>(x));
    }
    else if constexpr (sizeof(T) <= sizeof(unsigned long long))
    {
        result = __builtin_ffsll(static_cast<unsigned long long>(x));
    }
    return result;

#else
#error countr_zero has not been ported to this compiler
#endif
}

template <typename T>
int countl_one(T x) noexcept
{
    return countl_zero(~x);
}
template <typename T>
int countr_one(T x) noexcept
{
    return countr_zero(~x);
}
} // namespace vefs::utils

#endif
