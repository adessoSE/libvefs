#pragma once

#include <string_view>
#include <system_error>

#include <fmt/time.h>

#include <vefs/ext/outcome.hpp>

#include <vefs/disappointment/fwd.hpp>
#include <vefs/disappointment/error_detail.hpp>
#include <vefs/disappointment/error_domain.hpp>
#include <vefs/disappointment/error.hpp>
#include <vefs/disappointment/error_exception.hpp>
#include <vefs/disappointment/errc.hpp>
#include <vefs/disappointment/std_adapter.hpp>

namespace vefs::ed
{
    struct file_span
    {
        std::uint64_t begin;
        std::uint64_t end;
    };
}

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
            return format_to(ctx.begin(), "[{},{})", fspan.begin, fspan.end);
        }
    };
}

namespace vefs
{
    namespace outcome = ::outcome_v2_4995acdc;

    namespace detail
    {
        class result_no_value_policy
            : public outcome::policy::base
        {
        public:
            //! Performs a narrow check of state, used in the assume_value() functions.
            using base::narrow_value_check;

            //! Performs a narrow check of state, used in the assume_error() functions.
            using base::narrow_error_check;

            //! Performs a wide check of state, used in the value() functions.
            template <class Impl>
            static constexpr void wide_value_check(Impl &&self)
            {
                if (!base::_has_value(self))
                {
                    if (base::_has_error(self))
                    {
                        throw error_exception{ base::_error(std::move(self)) };
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

        class outcome_no_value_policy
            : public outcome::policy::base
        {
        public:
            //! Performs a narrow check of state, used in the assume_value() functions.
            using base::narrow_value_check;

            //! Performs a narrow check of state, used in the assume_error() functions.
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
                        throw error_exception{ base::_error(std::forward<Impl>(self)) };
                    }
                    if (base::_has_exception(self))
                    {
                        std::rethrow_exception(base::_exception(std::forward<Impl>(self)));
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
    }

    template <typename R, typename E = error>
    using result = outcome::basic_result<R, E, detail::result_no_value_policy>;

    template <typename R, typename E = error>
    using op_outcome = outcome::outcome<R, E, std::exception_ptr, detail::outcome_no_value_policy>;

    template <typename T, typename InjectFn>
    auto inject(result<T> rx, InjectFn &&injectFn)
        -> result<T>
    {
        if (rx.has_error())
        {
            injectFn(rx.assume_error());
        }
        return std::move(rx);
    }

    template <typename Fn, typename... Args>
    inline auto collect_disappointment(Fn &&fn, Args&&... args) noexcept
        -> vefs::op_outcome<typename decltype(fn(std::forward<Args>(args)...))::value_type>
    try
    {
        if (auto &&rx = fn(std::forward<Args>(args)...))
        {
            if constexpr (std::is_void_v<typename std::remove_reference_t<decltype(rx)>::value_type>)
            {
                return outcome::success();
            }
            else
            {
                return std::move(rx).assume_value();
            }
        }
        else
        {
            return std::move(rx).as_failure();
        }
    }
    catch (const std::bad_alloc &)
    {
        return errc::not_enough_memory;
    }
    catch (...)
    {
        return std::current_exception();
    }

    namespace ed
    {
        enum class wrapped_error_tag{};
        using wrapped_error = error_detail<wrapped_error_tag, error>;

        enum class error_code_tag{};
        using error_code = error_detail<error_code_tag, std::error_code>;

        enum class error_code_origin_tag{};
        using error_code_api_origin = error_detail<error_code_origin_tag, std::string_view>;

        enum class io_file_tag {};
        using io_file = error_detail<io_file_tag, std::string>;

        enum class archive_file_tag {};
        using archive_file = error_detail<archive_file_tag, std::string>;


        enum class archive_file_read_area_tag {};
        using archive_file_read_area = error_detail<archive_file_read_area_tag, file_span>;
        enum class archive_file_write_area_tag {};
        using archive_file_write_area = error_detail<archive_file_write_area_tag, file_span>;
    }

    auto collect_system_error()
        -> std::error_code;
}

#define VEFS_TRY_INJECT(stmt, injected) OUTCOME_TRY(vefs::inject((stmt), [&](auto &e) { e << (injected); }))
