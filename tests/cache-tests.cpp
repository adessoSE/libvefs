#include <vefs/detail/cache.hpp>
#include "boost-unit-test.hpp"


using namespace vefs::detail;
namespace bdata = boost::unit_test::data;

BOOST_AUTO_TEST_SUITE(cache_tests)

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

BOOST_AUTO_TEST_CASE(cache_ctor)
{
    using cache_t = cache<std::size_t, cached_value, 1023>;
    cache_t cx{ {} };

    auto h = cx.try_access(6487);
    BOOST_CHECK(!h);
    h = std::get<1>(cx.access(6487, 4, 9, nullptr));
    BOOST_CHECK(h);
    h = cx.try_access(6487);
    BOOST_CHECK(h);
}

BOOST_AUTO_TEST_SUITE_END()
