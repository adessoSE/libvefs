#include "boost-unit-test.hpp"
#include <vefs/detail/cache_car.hpp>

#include "test-utils.hpp"
#include <boost/test/unit_test_suite.hpp>
#include <boost/test/unit_test_log.hpp>
#include <boost/test/tools/interface.hpp>

using namespace vefs;
using namespace vefs::detail;
namespace bdata = boost::unit_test::data;

namespace vefs::detail
{
    std::ostream &operator<<(std::ostream &str, enum_bitset<cache_replacement_result> val)
    {
        using namespace std::string_view_literals;
        if (val == cache_replacement_result::succeeded)
        {
            str << "(replacement result:success)"sv;
        }
        else
        {
            std::string buf("(replacement result:"sv);
            if (val % cache_replacement_result::referenced)
            {
                buf = "referenced"sv;
            }
            if (val % cache_replacement_result::second_chance)
            {
                buf += "|second chance"sv.substr(buf.size() == 0);
            }
            if (val % cache_replacement_result::dirty)
            {
                buf += "|dirty"sv.substr(buf.size() == 0);
            }
            str << (buf += ")"sv);
        }
        return str;
    }
} // namespace vefs::detail

namespace cache_tests
{
    struct cached_value
    {
        cached_value(int v1, int v2, void *v3) noexcept
            : val1{v1}
            , val2{v2}
            , val3{v3}
        {
        }

        int val1;
        int val2;
        void *val3;
    };
} // namespace cache_tests

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
} // namespace fmt

BOOST_AUTO_TEST_SUITE(cache_tests)

using cache_t = cache_car<std::size_t, cached_value, 1023>;

BOOST_AUTO_TEST_CASE(cache_ctor)
{
    auto cx = std::make_unique<cache_t>(cache_t::notify_dirty_fn{});

    auto h = cx->try_access(6487);
    static_assert(std::is_same_v<decltype(h), cache_t::handle>);
    BOOST_TEST(!h);
    h = cx->access(6487, 4, 9, nullptr);
    BOOST_TEST(h);
    h = cx->try_access(6487);
    BOOST_TEST(h);
}

BOOST_AUTO_TEST_CASE(cache_handle_acquire_release)
{
    using cache_handle = cache_handle<cached_value>;
    using cache_page = cache_page<cached_value>;

    cache_page page;
    BOOST_TEST(page.is_dead());
    BOOST_TEST(!page.is_dirty());

    BOOST_TEST_REQUIRE(!page.try_acquire());
    BOOST_TEST(page.try_start_replace() == cache_replacement_result::succeeded);
    auto rx = page.finish_replace([](void *p) noexcept->result<cached_value *> {
        return new (p) cached_value(4, 10, nullptr);
    });
    TEST_RESULT_REQUIRE(rx);
    auto h = std::move(rx).assume_value();
    BOOST_TEST(h->val1 == 4);
    BOOST_TEST(h->val2 == 10);
    BOOST_TEST(h->val3 == nullptr);

    BOOST_TEST(page.try_start_replace() == cache_replacement_result::referenced);
    h.mark_dirty();
    BOOST_TEST(page.try_start_replace() %
               (cache_replacement_result::referenced & cache_replacement_result::dirty));
    h = nullptr;
    BOOST_TEST(page.try_start_replace() == cache_replacement_result::dirty);
    page.mark_clean();

    h = page.try_acquire();
    BOOST_TEST_REQUIRE(h);
    h = nullptr;

    BOOST_TEST(page.try_start_replace() == cache_replacement_result::second_chance);
    BOOST_TEST(page.try_start_replace() == cache_replacement_result::succeeded);
    page.cancel_replace();
}

BOOST_AUTO_TEST_CASE(try_access_returns_value_value_in_cache)
{
    using namespace vefs::utils;
    using namespace vefs::detail;
    using namespace cache_tests;

    auto cx = std::make_unique<cache_t>(cache_t::notify_dirty_fn{});

    cached_value value = {1, 2, nullptr};

    (void)cx->access(0, value);

    auto result = cx->try_access(0);

    auto cached_result =((aliasing_ref_ptr<cached_value,cache_page<cached_value>>*)&result)->get();
    
    BOOST_TEST(cached_result->val1 == value.val1);
    BOOST_TEST(cached_result->val2 == value.val2);
    BOOST_TEST(cached_result->val3 == value.val3);    
}

BOOST_AUTO_TEST_CASE(try_access_returns_zero_if_no_value_in_cache)
{
    using namespace vefs::utils;
    using namespace vefs::detail;
    using namespace cache_tests;

    auto cx = std::make_unique<cache_t>(cache_t::notify_dirty_fn{});

    cached_value value = {1, 2, nullptr};

    (void)cx->access(0, value);

    auto result = cx->try_access(1);

    BOOST_TEST(!result);
}


BOOST_AUTO_TEST_CASE(first_added_entry_gets_envicted_on_full_chace)
{
    auto cx = std::make_unique<cache_t>(cache_t::notify_dirty_fn{});
    constexpr auto max_entries = cache_t::max_entries;

    for (std::size_t i = 0; i < max_entries; ++i)
    {
        (void)cx->access(i, i, 0, nullptr);
    }

    (void)cx->access(1337, 1337, 0, nullptr);

    BOOST_TEST(!cx->try_access(0));
    BOOST_TEST(cx->try_access(1));
    BOOST_TEST(cx->try_access(1337));
}

BOOST_AUTO_TEST_CASE(second_chance_entry_gets_not_envicted_on_full_chace)
{
    using namespace vefs::utils;
    using namespace vefs::detail;
    using namespace cache_tests;

    auto cx = std::make_unique<cache_t>(cache_t::notify_dirty_fn{});
    constexpr auto max_entries = cache_t::max_entries;

    for (std::size_t i = 0; i < max_entries; ++i)
    {
        (void)cx->access(i, i, 0, nullptr);
    }
    (void)cx->try_access(0); //second chance

    cached_value new_value = {1337, 1338, nullptr};

    auto result = cx->access(1337, new_value);
    auto cached_result =
        ((aliasing_ref_ptr<cached_value, cache_page<cached_value>> *)&result)->get();

    BOOST_TEST(cx->try_access(0));
   
    BOOST_TEST(cached_result->val1 == new_value.val1);
    BOOST_TEST(cached_result->val2 == new_value.val2);
    BOOST_TEST(cached_result->val3 == new_value.val3);
}


BOOST_AUTO_TEST_SUITE_END()
