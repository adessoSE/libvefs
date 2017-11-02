#pragma once

#include <exception>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#include <boost/preprocessor/cat.hpp>

namespace vefs::utils
{
    template <typename T>
    constexpr T div_ceil(T dividend, T divisor)
    {
        static_assert(std::is_unsigned_v<T>);

        return dividend / divisor + (dividend && dividend % divisor != 0);
    }
    template <typename T, typename U>
    constexpr auto div_ceil(T dividend, U divisor)
        -> std::common_type_t<T, U>
    {
        using common_t = std::common_type_t<T, U>;
        return div_ceil(static_cast<common_t>(dividend), static_cast<common_t>(divisor));
    }

    template <std::uint8_t... Values>
    constexpr auto make_byte_array()
    {
        return std::array<std::byte, sizeof...(Values)>{
            std::byte{ Values }...
        };
    }


    template <typename Fn>
    struct scope_guard
    {
        BOOST_FORCEINLINE scope_guard(Fn &&fn)
            : mFn(std::forward<Fn>(fn))
        {
        }
        BOOST_FORCEINLINE ~scope_guard() noexcept
        {
            mFn();
        }

    private:
        Fn mFn;
    };

    enum class on_exit_scope
    {
    };

    template <typename Fn>
    BOOST_FORCEINLINE scope_guard<Fn> operator+(on_exit_scope, Fn &&fn)
    {
        return scope_guard<Fn>{std::forward<Fn>(fn)};
    }

    template <typename Fn>
    struct error_scope_guard
    {
        BOOST_FORCEINLINE error_scope_guard(Fn &&fn)
            : mFn(std::forward<Fn>(fn))
            , mUncaughtExceptions(std::uncaught_exceptions())
        {
        }
        BOOST_FORCEINLINE ~error_scope_guard() noexcept
        {
            if (mUncaughtExceptions < std::uncaught_exceptions())
            {
                mFn();
            }
        }

    private:
        Fn mFn;
        int mUncaughtExceptions;
    };

    enum class on_error_exit
    {
    };

    template <typename Fn>
    BOOST_FORCEINLINE error_scope_guard<Fn> operator+(on_error_exit, Fn &&fn)
    {
        return error_scope_guard<Fn>{std::forward<Fn>(fn)};
    }

#define VEFS_ANONYMOUS_VAR(id) BOOST_PP_CAT(id, __LINE__)
#define VEFS_SCOPE_EXIT auto VEFS_ANONYMOUS_VAR(_scope_exit_guard_) \
                            = ::vefs::utils::on_exit_scope{} + [&]()
#define VEFS_ERROR_EXIT auto VEFS_ANONYMOUS_VAR(_error_exit_guard_) \
                            = ::vefs::utils::on_error_exit{} + [&]()

}

namespace vefs::utils::detail
{
    constexpr bool is_hex_digit(char c)
    {
        return c >= '0' && c <= '9'
            || c >= 'a' && c <= 'f'
            || c >= 'A' && c <= 'F';
    }

    constexpr std::byte parse_hex_digit(char digit)
    {
        auto value = static_cast<int>(digit);
        return static_cast<std::byte>(
            (value - '0') * (value >= '0' && value <= '9')
            + (value + 10 - 'a') * (value >= 'a' && value <= 'f')
            + (value + 10 - 'A') * (value >= 'A' && value <= 'F')
        );
    }

    template <char First, std::size_t ArrSize>
    constexpr std::size_t parse_hex(std::array<std::byte, ArrSize> &out, std::size_t parsedSize)
    {
        static_assert(!First && First, "a byte array sequence must be defined by an even number of hex digits");
        return ~static_cast<size_t>(0);
    }

    template <char First, char Second, char... Chars, std::size_t ArrSize>
    constexpr std::size_t parse_hex(std::array<std::byte, ArrSize> &out, std::size_t parsedSize)
    {
        constexpr auto fCat = is_hex_digit(First);
        constexpr auto sCat = is_hex_digit(Second);

        static_assert(fCat || First == '\'');
        static_assert(sCat || Second == '\'');

        if constexpr(fCat && sCat)
        {
            out[parsedSize++] = parse_hex_digit(First) << 4 | parse_hex_digit(Second);

            if constexpr (sizeof...(Chars) > 0)
            {
                return parse_hex<Chars...>(out, parsedSize);
            }
            else
            {
                return parsedSize;
            }
        }
        else
        {
            return parse_hex<First == '\'' ? Second : First, Chars...>(out, parsedSize);
        }
    }

    template <char First, char Second, char... Chars>
    constexpr std::tuple<std::array<std::byte, sizeof...(Chars)/2>, std::size_t> parse_hex()
    {
        static_assert(First == '0' && (Second == 'x' || Second == 'X'));

        std::array<std::byte, sizeof...(Chars)/2> storage = {};

        auto numBytes = parse_hex<Chars...>(storage, 0);
        return { storage, numBytes };
    }
}

namespace vefs
{
    inline namespace blob_literals
    {
        template <char... Chars>
        constexpr auto operator""_as_bytes()
        {
            using namespace vefs::utils::detail;

            constexpr auto parseResult = parse_hex<Chars...>();

            auto data = std::get<0>(parseResult);

            if constexpr (data.size() == std::get<1>(parseResult))
            {
                return data;
            }
            else
            {
                std::array<std::byte, std::get<1>(parseResult)> result = {};
                for (size_t i = 0; i < result.size(); ++i)
                {
                    result[i] = data[i];
                }
                return result;
            }
        }
    }
}
