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

namespace vefs
{
    namespace outcome = ::outcome_v2_97cad4a6;

    namespace detail
    {
        class result_no_value_policy
            : private outcome::policy::all_narrow
        {
            //! Performs a narrow check of state, used in the assume_value() functions.
            using outcome::policy::all_narrow::narrow_value_check;

            //! Performs a narrow check of state, used in the assume_error() functions.
            using outcome::policy::all_narrow::narrow_error_check;

            //! Performs a wide check of state, used in the value() functions.
            template <class Impl>
            static constexpr void wide_value_check(Impl &&self)
            {
                if (!self.have_value())
                {
                    if (self.have_error())
                    {
                        throw error_exception{ self.assume_error() };
                    }
                    throw outcome::bad_result_access("no value");
                }
            }

            //! Performs a wide check of state, used in the error() functions.
            template <class Impl>
            static constexpr void wide_error_check(Impl &&self)
            {
                if (!self.have_error())
                {
                    throw outcome::bad_result_access("no error");
                }
            }
        };
    }

    template <typename R>
    using result = outcome::basic_result<R, error, detail::result_no_value_policy>;

    namespace ed
    {
        enum class wrapped_error_tag{};
        using wrapped_error = error_detail<wrapped_error_tag, error>;

        enum class error_code_tag{};
        using error_code = error_detail<error_code_tag, std::error_code>;

        enum class error_code_origin_tag{};
        using error_code_api_origin = error_detail<error_code_origin_tag, std::string_view>;
    }
}
