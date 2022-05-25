#pragma once

#include <string_view>
#include <system_error>
#include <variant>

#include <fmt/format.h>

#include <boost/predef.h>
#include <vefs/exceptions.hpp>

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(push, 3)
#pragma warning(disable : 6285)
#endif

#include <outcome/basic_outcome.hpp>
#include <outcome/basic_result.hpp>
#include <outcome/try.hpp>

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(pop)
#endif

#include <vefs/disappointment/errc.hpp>
#include <vefs/disappointment/error.hpp>
#include <vefs/disappointment/error_detail.hpp>
#include <vefs/disappointment/error_domain.hpp>
#include <vefs/disappointment/error_exception.hpp>
#include <vefs/disappointment/fwd.hpp>
#include <vefs/disappointment/std_adapter.hpp>
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
    auto format(const vefs::ed::file_span &fspan, FormatContext &ctx)
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
                throw error_exception{base::_error(std::move(self))};
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

class outcome_no_value_policy : public outcome::policy::base
{
public:
    //! Performs a narrow check of state, used in the assume_value()
    //! functions.
    using base::narrow_value_check;

    //! Performs a narrow check of state, used in the assume_error()
    //! functions.
    using base::narrow_error_check;

    using base::narrow_exception_check;

    //! Performs a wide check of state, used in the value() functions.
    template <class Impl>
    static constexpr void wide_value_check(Impl &&self)
    {
        if (!base::_has_value(self))
        {
            if (base::_has_error(self))
            {
                throw error_exception{base::_error(std::forward<Impl>(self))};
            }
            if (base::_has_exception(self))
            {
                std::rethrow_exception(
                        base::_exception(std::forward<Impl>(self)));
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
            throw outcome::bad_outcome_access("no error");
        }
    }

    template <class Impl>
    static constexpr void wide_exception_check(Impl &&self)
    {
        if (!base::_has_exception(self))
        {
            throw outcome::bad_outcome_access("no exception");
        }
    }
};
} // namespace detail

using oc::failure;
using oc::success;

template <typename R, typename E = error>
using result = oc::basic_result<R, E, detail::result_no_value_policy>;

template <typename R, typename E = error>
using op_outcome = oc::basic_outcome<R,
                                     E,
                                     std::exception_ptr,
                                     detail::outcome_no_value_policy>;

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
    if (auto ptr = new (std::nothrow) T(static_cast<Args &&>(args)...))
    {
        return std::unique_ptr<T>(ptr);
    }
    else
    {
        return errc::not_enough_memory;
    }
}

template <typename T, typename InjectFn>
auto inject(result<T> rx, InjectFn &&injectFn) -> result<T>
{
    if (rx.has_error())
    {
        std::forward<InjectFn>(injectFn)(rx.assume_error());
    }
    return std::move(rx);
}

template <typename T, typename InjectFn>
auto inject(llfio::byte_io_handle::io_result<T> rx, InjectFn &&injectFn)
        -> llfio::result<T>
{
    if (rx.has_error())
    {
        std::forward<InjectFn>(injectFn)(rx.assume_error());
    }
    return std::move(rx);
}

template <typename T>
struct can_result_contain_failure;

template <typename R, typename EC, typename NVP>
struct can_result_contain_failure<oc::basic_result<R, EC, NVP>>
    : std::bool_constant<!std::is_void_v<EC>>
{
};
template <typename R, typename EC, typename EP, typename NVP>
struct can_result_contain_failure<oc::basic_outcome<R, EC, EP, NVP>>
    : std::bool_constant<!std::is_void_v<EC> || !std::is_void_v<EP>>
{
};

template <typename T>
inline constexpr bool can_result_contain_failure_v
        = can_result_contain_failure<T>::value;

template <typename Fn, typename... Args>
inline auto collect_disappointment_no_catch(Fn &&fn, Args &&...args) noexcept
        -> decltype(auto)
{
    using invoke_result_type = std::invoke_result_t<Fn, Args...>;
    if constexpr (std::is_void_v<invoke_result_type>)
    {
        std::invoke(std::forward<Fn>(fn), std::forward<Args>(args)...);
        return result<std::monostate, void>(success());
    }
    else if constexpr (
            oc::is_basic_result_v<
                    invoke_result_type> || oc::is_basic_outcome_v<invoke_result_type>)
    {
        return std::invoke(std::forward<Fn>(fn), std::forward<Args>(args)...);
    }
    else
    {
        return result<invoke_result_type, void>{success(std::invoke(
                std::forward<Fn>(fn), std::forward<Args>(args)...))};
    }
}

template <typename Fn, typename... Args>
using collected_disappointment_t = typename std::conditional_t<
        std::is_nothrow_invocable_v<Fn, Args...>,
        result<void>,
        op_outcome<void>>::
        template rebind<typename decltype(collect_disappointment_no_catch(
                std::declval<Fn>(), std::declval<Args>()...))::value_type>;

template <typename Fn, typename... Args>
inline auto collect_disappointment(Fn &&fn, Args &&...args) noexcept
        -> decltype(auto)
{
    using invoke_result_type = std::invoke_result_t<Fn, Args...>;
    if constexpr (std::is_nothrow_invocable_v<Fn, Args...>)
    {
        return collect_disappointment_no_catch(std::forward<Fn>(fn),
                                               std::forward<Args>(args)...);
    }
    else
    {
        using result_type = op_outcome<typename invoke_result_type::value_type>;
        try
        {
            return result_type{collect_disappointment_no_catch(
                    std::forward<Fn>(fn), std::forward<Args>(args)...)};
        }
        catch (const std::bad_alloc &)
        {
            return result_type{failure(errc::not_enough_memory)};
        }
        catch (...)
        {
            return result_type{failure(std::current_exception())};
        }
    }

    constexpr bool is_nothrow_invocable_v
            = std::is_nothrow_invocable_v<Fn, Args...>;
    using invoke_result_type = std::invoke_result_t<Fn, Args...>;
    if constexpr (std::is_void_v<invoke_result_type>)
    {
        if constexpr (is_nothrow_invocable_v)
        {
            std::invoke(std::forward<Fn>(fn), std::forward<Args>(args)...);
            return result<void>(success());
        }
        else
        {
            try
            {
                std::invoke(std::forward<Fn>(fn), std::forward<Args>(args)...);
                return op_outcome<void>(success());
            }
            catch (const std::bad_alloc &)
            {
                return op_outcome<void>(failure(errc::not_enough_memory));
            }
            catch (...)
            {
                return op_outcome<void>(std::current_exception());
            }
        }
    }
    else if constexpr (
            oc::is_basic_result_v<
                    invoke_result_type> || oc::is_basic_outcome_v<invoke_result_type>)
    {
        if constexpr (is_nothrow_invocable_v)
        {
            return std::invoke(std::forward<Fn>(fn),
                               std::forward<Args>(args)...);
        }
        else
        {
            using outcome_type
                    = op_outcome<typename invoke_result_type::value_type,
                                 typename invoke_result_type::error_type>;
            try
            {
                return outcome_type(std::invoke(std::forward<Fn>(fn),
                                                std::forward<Args>(args)...));
            }
            catch (const std::bad_alloc &)
            {
                return outcome_type(failure(errc::not_enough_memory));
            }
            catch (...)
            {
                return outcome_type(std::current_exception());
            }
        }
    }
    else
    {
        if constexpr (is_nothrow_invocable_v)
        {
            return result<invoke_result_type>{success(std::invoke(
                    std::forward<Fn>(fn), std::forward<Args>(args)...))};
        }
        else
        {
            using outcome_type = op_outcome<invoke_result_type>;
            try
            {
                return outcome_type(success(std::invoke(
                        std::forward<Fn>(fn), std::forward<Args>(args)...)));
            }
            catch (const std::bad_alloc &)
            {
                return outcome_type(failure(errc::not_enough_memory));
            }
            catch (...)
            {
                return outcome_type(std::current_exception());
            }
        }
    }
}

namespace ed
{
enum class wrapped_error_tag
{
};
using wrapped_error = error_detail<wrapped_error_tag, error>;

enum class error_code_tag
{
};
using error_code = error_detail<error_code_tag, std::error_code>;

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

auto collect_system_error() -> std::error_code;
} // namespace vefs

#define VEFS_TRY(...) OUTCOME_TRY(__VA_ARGS__)

#define VEFS_TRY_INJECT(stmt, injected)                                        \
    VEFS_TRY(vefs::inject((stmt), [&](auto &e) mutable { e << injected; }))
