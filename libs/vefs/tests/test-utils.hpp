#pragma once

#include <ostream>

#include <fmt/format.h>

#include <vefs/disappointment.hpp>
#include <vefs/utils/random.hpp>
#include <vefs/detail/cache.hpp>

struct test_rng : vefs::utils::xoroshiro128plus
{
    // default initialize the test rng to the first 32 hex digits of pi
    // pi is random enough to be a good seed and hard coding it here
    // guarantees that the test cases are reproducible
    test_rng()
        : xoroshiro128plus(0x243F'6A88'85A3'08D3ull, 0x1319'8A2E'0370'7344ull)
    {
    }
    using xoroshiro128plus::xoroshiro128plus;
};

namespace vefs
{
    inline std::ostream & operator<<(std::ostream &s, const error_domain &domain)
    {
        using namespace fmt::literals;
        s << "[error_domain|{}]"_format(domain.name());
        return s;
    }

    template <typename T>
    inline auto check_result(const result<T> &rx)
        -> boost::test_tools::predicate_result
    {
        if (!rx)
        {
            boost::test_tools::predicate_result prx{ false };
            prx.message() << rx.assume_error();
            return std::move(prx);
        }
        return true;
    }
}

#define TEST_RESULT(...) BOOST_TEST( (::vefs::check_result((__VA_ARGS__))) )
#define TEST_RESULT_REQUIRE(...) BOOST_TEST_REQUIRE( (::vefs::check_result((__VA_ARGS__))) )

namespace fmt
{
    template <typename T>
    struct formatter<vefs::detail::cache_handle<T>>
    {
        template <typename ParseContext>
        constexpr auto parse(ParseContext &ctx)
        {
            return ctx.begin();
        }

        template <typename FormatContext>
        auto format(const vefs::detail::cache_handle<T> &h, FormatContext &ctx)
        {
            if (h)
            {
                return format_to(ctx.begin(), "{}", *h);
            }
            else
            {
                return format_to(ctx.begin(), "[nullptr cache_handle]");
            }
        }
    };
}

namespace vefs::detail
{
    template <typename T>
    inline auto operator<<(std::ostream &s, const cache_handle<T> &h)
        -> std::ostream &
    {
        return s << fmt::format(FMT_STRING("{}"), h);
    }
}