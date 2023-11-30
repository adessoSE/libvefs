#pragma once

#include <cassert>

#include <concepts>
#include <exception>
#include <functional>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#include <boost/predef/compiler.h>
#include <boost/preprocessor/cat.hpp>

#include <vefs/exceptions.hpp>

namespace vefs::utils
{

template <typename T, typename... Ts>
concept any_of = (std::same_as<T, Ts> || ...);

template <typename T, typename... Ts>
concept none_of = (!std::same_as<T, Ts> && ...);

template <typename T>
concept integer = std::integral<T>
                  && none_of<std::remove_cv_t<T>,
                             bool,
                             char,
                             wchar_t,
                             char8_t,
                             char16_t,
                             char32_t>;

template <typename T>
concept signed_integer = integer<T> && std::is_signed_v<T>;

template <typename T>
concept unsigned_integer = integer<T> && !signed_integer<T>;

template <typename T>
constexpr auto div_ceil(T dividend, T divisor) -> T
{
    return dividend / divisor + (dividend % divisor != 0);
}
template <typename T, typename U>
constexpr auto div_ceil(T dividend, U divisor) -> std::common_type_t<T, U>
{
    using common_t = std::common_type_t<T, U>;
    return utils::div_ceil(static_cast<common_t>(dividend),
                           static_cast<common_t>(divisor));
}

template <unsigned_integer T, unsigned_integer U>
constexpr auto round_up(T value, U multiple) noexcept
        -> std::common_type_t<T, U>
{
    return utils::div_ceil(value, multiple) * multiple;
}

#if defined(BOOST_COMP_MSVC_AVAILABLE)
#pragma warning(push)
// > unary minus operator applied to unsigned type, result still unsigned
// which is exactly what we want in this case #twos-complement.
#pragma warning(disable : 4146)
#endif

template <unsigned_integer T, unsigned_integer U>
constexpr auto round_up_p2(T value, U multiple) noexcept
        -> std::common_type_t<T, U>
{
    return (value + multiple - 1) & -multiple;
}

#if defined(BOOST_COMP_MSVC_AVAILABLE)
#pragma warning(pop)
#endif

template <typename T, typename U>
constexpr auto mod(T k, U n) -> std::common_type_t<T, U>
{
    if constexpr (std::is_unsigned_v<T>)
    {
        return k % n;
    }
    else
    {
        assert(n > 0);

        auto const r = k % n;
        return r < 0 ? k + n : k;
    }
}

constexpr auto upow(std::uint64_t x, std::uint64_t e)
{
    std::uint64_t result = 1;
    while (e != 0u)
    {
        if ((e & 1) != 0u)
        {
            result *= x;
        }

        e >>= 1;
        x *= x;
    }
    return result;
}

template <std::uint8_t... Values>
constexpr auto make_byte_array()
{
    return std::array<std::byte, sizeof...(Values)>{std::byte{Values}...};
}
template <typename... Ts>
constexpr auto make_byte_array(Ts &&...ts)
        -> std::array<std::byte, sizeof...(Ts)>
{
    static_assert((... && std::is_integral_v<Ts>));
    if ((... || (static_cast<std::make_unsigned_t<Ts>>(ts) > 0xFFu)))
    {
        throw std::logic_error("tried to make a byte with a value > 0xFF");
    }
    return {static_cast<std::byte>(ts)...};
}
template <std::ranges::contiguous_range R>
    requires std::convertible_to<std::ranges::range_value_t<R>, char>
inline auto as_string_view(R &r) noexcept -> std::string_view
{
    return std::string_view{std::ranges::data(r), std::ranges::size(r)};
}
constexpr auto is_null_byte(const std::byte value) noexcept -> bool
{
    return value == std::byte{};
}
constexpr auto is_non_null_byte(const std::byte value) noexcept -> bool
{
    return !is_null_byte(value);
}

template <typename R, typename Fn, std::size_t... is, typename... Args>
constexpr auto
sequence_init(Fn &&initFn, std::index_sequence<is...>, Args &&...args) -> R
{
    return R{initFn(args..., is)...};
}

template <typename R, std::size_t N, typename Fn, typename... Args>
constexpr auto sequence_init(Fn &&initFn, Args &&...args) -> R
{
    return sequence_init<R>(std::forward<Fn>(initFn),
                            std::make_index_sequence<N>{},
                            std::forward<Args>(args)...);
}

template <typename T>
struct remove_cvref
{
    using type = std::remove_cv_t<std::remove_reference_t<T>>;
};

template <typename T>
using remove_cvref_t = typename remove_cvref<T>::type;

template <typename F, typename... Args>
inline auto error_code_scope(F &&f, Args &&...args) -> decltype(auto)
{
    std::error_code ec;
    if constexpr (std::is_void_v<decltype(f(std::forward<Args>(args)..., ec))>)
    {
        f(std::forward<Args>(args)..., ec);
        if (ec)
        {
            BOOST_THROW_EXCEPTION(std::system_error(ec));
        }
    }
    else
    {
        decltype(auto) result = f(std::forward<Args>(args)..., ec);
        if (ec)
        {
            BOOST_THROW_EXCEPTION(std::system_error(ec));
        }
        return result;
    }
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
BOOST_FORCEINLINE auto operator+(on_exit_scope, Fn &&fn) -> scope_guard<Fn>
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
BOOST_FORCEINLINE auto operator+(on_error_exit, Fn &&fn)
        -> error_scope_guard<Fn>
{
    return error_scope_guard<Fn>{std::forward<Fn>(fn)};
}

// NOLINTBEGIN(cppcoreguidelines-macro-usage)

#define VEFS_ANONYMOUS_VAR(id) BOOST_PP_CAT(id, __LINE__)
#define VEFS_SCOPE_EXIT                                                        \
    auto VEFS_ANONYMOUS_VAR(_scope_exit_guard_)                                \
            = ::vefs::utils::on_exit_scope{} + [&]()
#define VEFS_ERROR_EXIT                                                        \
    auto VEFS_ANONYMOUS_VAR(_error_exit_guard_)                                \
            = ::vefs::utils::on_error_exit{} + [&]()

// NOLINTEND(cppcoreguidelines-macro-usage)

} // namespace vefs::utils
