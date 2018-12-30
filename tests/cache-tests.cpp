#include <vefs/detail/cache.hpp>
#include "boost-unit-test.hpp"

#include "test-utils.hpp"

using namespace vefs::detail;
namespace bdata = boost::unit_test::data;

namespace cache_tests
{
    struct cached_value
    {
        cached_value(int v1, int v2, void *v3)
            : val1{ v1 }
            , val2{ v2 }
            , val3{ v3 }
        {
        }

        int val1;
        int val2;
        void *val3;
    };
}

namespace fmt
{
    template <>
    struct formatter<cache_tests::cached_value>
    {
        template <typename ParseContext>
        constexpr auto parse(ParseContext &ctx)
        {
            return ctx.begin();
        }

        template <typename FormatContext>
        auto format(const cache_tests::cached_value &h, FormatContext &ctx)
        {
            return format_to(ctx.begin(), "[{},{},{}]", h.val1, h.val2, h.val3);
        }
    };
}

BOOST_AUTO_TEST_SUITE(cache_tests)


BOOST_AUTO_TEST_CASE(cache_ctor)
{
    using cache_t = cache<std::size_t, cached_value, 1023>;
    cache_t cx{ {} };

    auto h = cx.try_access(6487);
    BOOST_TEST(!h);
    auto arx = cx.access_w_inplace_ctor(6487, 4, 9, nullptr);
    TEST_RESULT_REQUIRE(arx);
    h = std::move(arx).assume_value();
    BOOST_TEST(h);
    h = cx.try_access(6487);
    BOOST_TEST(h);
}

BOOST_AUTO_TEST_SUITE_END()
