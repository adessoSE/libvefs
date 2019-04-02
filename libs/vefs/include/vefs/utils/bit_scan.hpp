#pragma once

#include <cstddef>
#include <cstring>

#include <limits>
#include <type_traits>

#include <boost/predef.h>

#if defined BOOST_COMP_MSVC_AVAILABLE
#include <intrin.h>
#endif

namespace vefs::utils::detail
{
    template <typename T, typename = void>
    struct has_BitScanForward : std::false_type
    {
    };
    template <typename T>
    struct has_BitScanForward<T, std::void_t<decltype(_BitScanForward(nullptr, std::declval<T>()))>>
        : std::true_type
    {
    };
    template <typename T>
    constexpr bool has_BitScanForward_v = has_BitScanForward<T>::value;

    template <typename T, typename = void>
    struct has_BitScanForward64 : std::false_type
    {
    };
    template <typename T>
    struct has_BitScanForward64<
        T, std::void_t<decltype(_BitScanForward64(nullptr, std::declval<T>()))>> : std::true_type
    {
    };
    template <typename T>
    constexpr bool has_BitScanForward64_v = has_BitScanForward64<T>::value;

    template <typename T, typename = void>
    struct has_builtin_ffs : std::false_type
    {
    };
    template <typename T>
    struct has_builtin_ffs<T, std::void_t<decltype(__builtin_ffs(std::declval<T>()))>>
        : std::true_type
    {
    };
    template <typename T>
    constexpr bool has_builtin_ffs_v = has_builtin_ffs<T>::value;

    template <typename T, typename = void>
    struct has_builtin_ffsll : std::false_type
    {
    };
    template <typename T>
    struct has_builtin_ffsll<T, std::void_t<decltype(__builtin_ffsll(std::declval<T>()))>>
        : std::true_type
    {
    };
    template <typename T>
    constexpr bool has_builtin_ffsll_v = has_builtin_ffsll<T>::value;
} // namespace vefs::utils::detail

namespace vefs::utils
{
    template <typename T>
    inline bool bit_scan(std::size_t &pos, T data)
    {
        using namespace vefs::utils::detail;

        static_assert(std::is_integral_v<T>);
        static_assert(std::is_unsigned_v<T>);

        if constexpr (has_BitScanForward_v<T> && sizeof(T) <= 4)
        {
            unsigned long pos_impl;
            _BitScanForward(&pos_impl, data);
            pos = static_cast<std::size_t>(pos_impl);
            return data != 0;
        }
        else if constexpr (has_builtin_ffs_v<T> && sizeof(T) <= 4)
        {
            pos = static_cast<std::size_t>(__builtin_ffs(data)) - 1;
            return pos != std::numeric_limits<std::size_t>::max();
        }
        else if constexpr (has_BitScanForward64_v<T> && sizeof(T) <= 8)
        {
            unsigned long pos_impl;
            _BitScanForward64(&pos_impl, data);
            pos = static_cast<std::size_t>(pos_impl);
            return data != 0;
        }
        else if constexpr (has_builtin_ffsll_v<T> && sizeof(T) <= 8)
        {
            pos = static_cast<std::size_t>(__builtin_ffsll(data)) - 1;
            return pos != std::numeric_limits<std::size_t>::max();
        }
        else if constexpr (sizeof(data) <= sizeof(std::size_t))
        {
            // generic algorithm
            for (auto i = 0; i < std::numeric_limits<T>::digits; ++i)
            {
                if (data & (1 << i))
                {
                    pos = i;
                    return true;
                }
            }
            return false;
        }
        else
        {
            // in case T is greater than std::size_t
            constexpr std::size_t num_parts =
                (sizeof(data) + sizeof(std::size_t) - 1) / sizeof(std::size_t);

            for (auto i = 0; i < num_parts; ++i)
            {
                std::size_t mem{0};
                if constexpr (sizeof(data) % sizeof(std::size_t) != 0)
                {
                    const auto last = i == num_parts - 1;
                    std::memcpy(&mem, &data + i * sizeof(std::size_t),
                                last ? sizeof(data) % sizeof(std::size_t) : sizeof(std::size_t));
                }
                else
                {
                    std::memcpy(&mem, &data + i * sizeof(std::size_t), sizeof(std::size_t));
                }

                if (bit_scan(pos, mem))
                {
                    pos += i * std::numeric_limits<std::size_t>::digits;
                    return true;
                }
            }
            return false;
        }
    }
} // namespace vefs::utils
