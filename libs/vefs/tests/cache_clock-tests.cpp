#include "boost-unit-test.hpp"
#include <vefs/detail/cache_car.hpp>
#include <boost/test/unit_test_suite.hpp>
#include <boost/test/unit_test_log.hpp>
#include <boost/test/tools/interface.hpp>


BOOST_AUTO_TEST_SUITE(cache_clock_tests)

    BOOST_AUTO_TEST_CASE(push_back_adds_an_entry)
    {
        auto cache_clock = vefs::detail::cache_clock<5>();
        cache_clock.push_back(2);

        auto result = cache_clock.size();

        BOOST_TEST(result == 1);
    }

    BOOST_AUTO_TEST_CASE(init_size_zero)
    {
        auto cache_clock = vefs::detail::cache_clock<5>();

        auto result = cache_clock.size();

        BOOST_TEST(result == 0);
    }

    BOOST_AUTO_TEST_CASE(pop_front_removes_entry)
    {
        auto cache_clock = vefs::detail::cache_clock<5>();
        cache_clock.push_back(2);
        cache_clock.push_back(2);

        cache_clock.pop_front();
        auto result = cache_clock.size();

        BOOST_TEST(result == 1);
    }

    BOOST_AUTO_TEST_CASE(clear_clock_sets_size_zero)
    {
        auto cx = vefs::detail::cache_clock<5>();
        cx.push_back(2);
        cx.push_back(2);

        cx.clear();

        BOOST_TEST(cx.size() == 0);
    }

    BOOST_AUTO_TEST_CASE(clear_clock_sets_target_size_zero)
    {
        auto cache_clock = vefs::detail::cache_clock<5>();
        cache_clock.size_target(1);
        cache_clock.push_back(2);
        cache_clock.push_back(2);

        cache_clock.clear();

        BOOST_TEST(cache_clock.size_target() == 0);
    }


    BOOST_AUTO_TEST_CASE(pop_front_removes_the_first_entry)
    {
        auto cache_clock = vefs::detail::cache_clock<5>();
        cache_clock.push_back(2);
        cache_clock.push_back(2);

        cache_clock.pop_front();
        auto result = cache_clock.pop_front();

        BOOST_TEST(result == 1);
    }


BOOST_AUTO_TEST_SUITE_END()
