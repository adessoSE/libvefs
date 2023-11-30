#pragma once

#include <concepts>
#include <new>
#include <string_view>
#include <variant>

#include <fmt/format.h>

#include <boost/predef.h>
#include <vefs/exceptions.hpp>

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(push, 3)
#pragma warning(disable : 6285)
#endif

#include <outcome/bad_access.hpp>
#include <outcome/experimental/status_result.hpp>
#include <outcome/try.hpp>
#include <status-code/error.hpp>
#include <status-code/std_error_code.hpp>

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(pop)
#endif

#include <vefs/disappointment/errc.hpp>
#include <vefs/disappointment/error_detail.hpp>
#include <vefs/disappointment/generic_errc.hpp>
#include <vefs/llfio.hpp>

namespace vefs::ed
{
struct file_span
{
    std::uint64_t begin;
    std::uint64_t end;
};
} // namespace vefs::ed

namespace fmt
{
template <>
struct formatter<vefs::ed::file_span>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext &ctx)
    {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(vefs::ed::file_span const &fspan, FormatContext &ctx)
    {
        using namespace std::string_view_literals;
        return fmt::format_to(ctx.out(), "[{},{})"sv, fspan.begin, fspan.end);
    }
};
} // namespace fmt

namespace vefs
{
namespace outcome = OUTCOME_V2_NAMESPACE;
namespace oc = OUTCOME_V2_NAMESPACE;

namespace detail
{
class result_no_value_policy : public outcome::policy::base
{
public:
    //! Performs a narrow check of state, used in the assume_value()
    //! functions.
    using base::narrow_value_check;

    //! Performs a narrow check of state, used in the assume_error()
    //! functions.
    using base::narrow_error_check;

    //! Performs a wide check of state, used in the value() functions.
    template <class Impl>
    static constexpr void wide_value_check(Impl &&self)
    {
        if (!base::_has_value(self))
        {
            if (base::_has_error(self))
            {
                // moving lvalues is expected in this case.
                // NOLINTNEXTLINE(bugprone-move-forwarding-reference)
                base::_error(std::move(self)).throw_exception();
            }
            throw outcome::bad_result_access("no value");
        }
    }

    //! Performs a wide check of state, used in the error() functions.
    template <class Impl>
    static constexpr void wide_error_check(Impl &&self)
    {
        if (!base::_has_error(self))
        {
            throw outcome::bad_result_access("no error");
        }
    }
};

} // namespace detail

using oc::failure;
using oc::success;

template <typename R, typename E = system_error::error>
using result = oc::basic_result<R, E, detail::result_no_value_policy>;

template <typename T, typename U>
    requires std::is_enum_v<std::remove_cvref_t<T>>
             && std::derived_from<std::remove_cvref_t<U>,
                                  detail::error_detail_base>
auto operator<<(T &&c, U &&) noexcept -> std::remove_cvref_t<T>
{
    return c;
}

} // namespace vefs

SYSTEM_ERROR2_NAMESPACE_BEGIN

template <typename T, typename U>
    requires std::derived_from<std::remove_cvref_t<U>,
                               vefs::detail::error_detail_base>
auto operator<<(status_code<T> &&c, U &&) noexcept -> status_code<T>
{
    return static_cast<status_code<T> &&>(c);
}
template <typename T, typename U>
    requires std::derived_from<std::remove_cvref_t<U>,
                               vefs::detail::error_detail_base>
auto operator<<(status_code<T> &c, U &&) noexcept -> status_code<T> &
{
    return c;
}

SYSTEM_ERROR2_NAMESPACE_END

namespace vefs
{

template <typename T, typename... Args>
    requires(!std::is_array_v<T>)
auto make_unique_nothrow(Args &&...args) noexcept(
        std::is_nothrow_constructible_v<T, decltype(args)...>)
        -> std::unique_ptr<T>
{
    return std::unique_ptr<T>(new (std::nothrow)
                                      T(static_cast<Args &&>(args)...));
}

template <typename T, typename... Args>
    requires(!std::is_array_v<T>)
auto make_unique_rx(Args &&...args) noexcept(
        std::is_nothrow_constructible_v<T, decltype(args)...>)
        -> result<std::unique_ptr<T>>
{
    result<std::unique_ptr<T>> rx{std::unique_ptr<T>{
            new (std::nothrow) T(static_cast<Args &&>(args)...)}};
    if (rx.assume_value().get() == nullptr) [[unlikely]]
    {
        rx = errc::not_enough_memory;
    }
    return rx;
}

template <typename T, typename InjectFn>
auto inject(result<T> rx, InjectFn &&injectFn) -> result<T>
{
    if (rx.has_error())
    {
        std::forward<InjectFn>(injectFn)(rx.assume_error());
    }
    return rx;
}

template <typename T, typename InjectFn>
auto inject(llfio::byte_io_handle::io_result<T> rx, InjectFn &&injectFn)
        -> llfio::result<T>
{
    if (rx.has_error())
    {
        std::forward<InjectFn>(injectFn)(rx.assume_error());
    }
    return rx;
}

template <typename T>
concept tryable = requires(T &&t) {
    {
        oc::try_operation_has_value(t)
    } -> std::same_as<bool>;
    {
        oc::try_operation_return_as(static_cast<T &&>(t))
    } -> std::convertible_to<result<void>>;
    oc::try_operation_extract_value(static_cast<T &&>(t));
};

template <tryable T>
using result_value_t
        = std::remove_cvref_t<decltype(oc::try_operation_extract_value(
                std::declval<T &&>()))>;

template <typename T, typename R>
concept tryable_result
        = tryable<T> && std::convertible_to<result_value_t<T>, R>;

namespace ed
{
enum class wrapped_error_tag
{
};
using wrapped_error = error_detail<wrapped_error_tag, system_error::error>;

enum class error_code_tag
{
};
using error_code = error_detail<error_code_tag, system_error::error>;

enum class error_code_origin_tag
{
};
using error_code_api_origin
        = error_detail<error_code_origin_tag, std::string_view>;

enum class io_file_tag
{
};
using io_file = error_detail<io_file_tag, std::string>;

enum class archive_file_tag
{
};
using archive_file = error_detail<archive_file_tag, std::string>;

enum class archive_file_read_area_tag
{
};
using archive_file_read_area
        = error_detail<archive_file_read_area_tag, file_span>;
enum class archive_file_write_area_tag
{
};
using archive_file_write_area
        = error_detail<archive_file_write_area_tag, file_span>;
} // namespace ed

auto collect_system_error() -> system_error::system_code;

} // namespace vefs

// NOLINTBEGIN(cppcoreguidelines-macro-usage)

#define VEFS_TRY(...) OUTCOME_TRY(__VA_ARGS__)

#define VEFS_TRY_INJECT(stmt, injected) VEFS_TRY(stmt)
// #define VEFS_TRY_INJECT(stmt, injected)
//     VEFS_TRY(vefs::inject((stmt), [&](auto &_vefsError) mutable
//                           { _vefsError << injected; }))

// NOLINTEND(cppcoreguidelines-macro-usage)
