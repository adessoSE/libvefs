#include "../src/detail/cache_car.hpp"
#include "boost-unit-test.hpp"

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
            return format_to(ctx.out(), "[{},{},{}]", h.val1, h.val2, h.val3);
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

BOOST_AUTO_TEST_CASE(cache_handle_initializes_dead_and_not_dirty)
{
    cache_page<cached_value> page;

    BOOST_TEST(page.is_dead());
    BOOST_TEST(!page.is_dirty());

    BOOST_TEST(!page.try_acquire());
}

BOOST_AUTO_TEST_CASE(finish_replace_for_replaceable_handle_returns_object)
{
    cache_page<cached_value> page;

    page.try_start_replace();

    auto rx = page.finish_replace([](void *p) noexcept->result<cached_value *> {
        return new (p) cached_value(4, 10, nullptr);
    });
    TEST_RESULT_REQUIRE(rx);
    auto h = std::move(rx).assume_value();
    BOOST_TEST(h->val1 == 4);
    BOOST_TEST(h->val2 == 10);
    BOOST_TEST(h->val3 == nullptr);
}

BOOST_AUTO_TEST_CASE(replacing_of_referenced_object_returns_referenced)
{
    cache_page<cached_value> page;

    page.try_start_replace();

    auto rx = page.finish_replace([](void *p) noexcept->result<cached_value *> {
        return new (p) cached_value(4, 10, nullptr);
    });

    TEST_RESULT_REQUIRE(rx);

    auto replace_existing_try = page.try_start_replace();

    BOOST_TEST(replace_existing_try == cache_replacement_result::referenced);
}

BOOST_AUTO_TEST_CASE(try_start_replace_for_dirty_and_unreferenced_returns_dirty)
{
    cache_page<cached_value> page;
    page.try_start_replace();

    auto rx = page.finish_replace([](void *p) noexcept->result<cached_value *> {
        return new (p) cached_value(4, 10, nullptr);
    });
    TEST_RESULT_REQUIRE(rx);
    auto h = std::move(rx).assume_value();

    h.mark_dirty();
    h = nullptr;

    BOOST_TEST(page.try_start_replace() == cache_replacement_result::dirty);
}

BOOST_AUTO_TEST_CASE(try_start_replace_for_dirty_and_referenced_returns_referenced)
{
    cache_page<cached_value> page;
    page.try_start_replace();

    auto rx = page.finish_replace([](void *p) noexcept->result<cached_value *> {
        return new (p) cached_value(4, 10, nullptr);
    });
    TEST_RESULT_REQUIRE(rx);
    auto h = std::move(rx).assume_value();

    h.mark_dirty();
    BOOST_TEST(page.try_start_replace() == cache_replacement_result::referenced );
}

BOOST_AUTO_TEST_CASE(try_acquire_sets_second_chance_bit)
{
    cache_page<cached_value> page;
    page.try_start_replace();

    auto rx = page.finish_replace([](void *p) noexcept->result<cached_value *> {
        return new (p) cached_value(4, 10, nullptr);
    });
    TEST_RESULT_REQUIRE(rx);
    BOOST_TEST(!rx.has_error());

    auto h = page.try_acquire();

    BOOST_TEST(page.try_start_replace() == cache_replacement_result::second_chance);
}

BOOST_AUTO_TEST_CASE(try_peek_not_sets_second_chance_bit)
{
    cache_page<cached_value> page;
    page.try_start_replace();

    auto rx = page.finish_replace([](void *p) noexcept->result<cached_value *> {
        return new (p) cached_value(4, 10, nullptr);
    });
    TEST_RESULT_REQUIRE(rx);
    BOOST_TEST(!rx.has_error());

    auto h = page.try_peek();

    BOOST_TEST(page.try_start_replace() != cache_replacement_result::second_chance);
}

BOOST_AUTO_TEST_CASE(try_start_replace_succeeds_on_second_chance_on_second_try)
{
    cache_page<cached_value> page;
    page.try_start_replace();

    auto rx = page.finish_replace([](void *p) noexcept->result<cached_value *> {
        return new (p) cached_value(4, 10, nullptr);
    });
    TEST_RESULT_REQUIRE(rx);
    BOOST_TEST(!rx.has_error());
    auto h = std::move(rx).assume_value();
    h = nullptr;

    h = page.try_acquire();
    h.mark_dirty();
    page.mark_clean();

    h = nullptr;

    (void)page.try_start_replace();

    BOOST_TEST(page.try_start_replace() == cache_replacement_result::succeeded);
}

BOOST_AUTO_TEST_CASE(mark_dirty_returns_true_if_handle_already_dirty)
{
    cache_page<cached_value> page;
    page.try_start_replace();

    auto rx = page.finish_replace([](void *p) noexcept->result<cached_value *> {
        return new (p) cached_value(4, 10, nullptr);
    });
    auto h = std::move(rx).assume_value();

    BOOST_TEST(!(h.mark_dirty()));
    BOOST_TEST(h.mark_dirty());
}


BOOST_AUTO_TEST_CASE(try_access_returns_value_in_cache)
{
    using namespace vefs::utils;

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

    auto cx = std::make_unique<cache_t>(cache_t::notify_dirty_fn{});

    cached_value value = {1, 2, nullptr};

    (void)cx->access(0, value);

    auto result = cx->try_access(1);

    BOOST_TEST(!result);
}


BOOST_AUTO_TEST_CASE(first_added_entry_gets_envicted_on_full_cache)
{
    auto cx = std::make_unique<cache_t>(cache_t::notify_dirty_fn{});
    constexpr auto max_entries = cache_t::max_entries;

    for (std::size_t i = 0; i < max_entries; ++i)
    {
        (void)cx->access(i, static_cast<int>(i), 0, nullptr);
    }

    (void)cx->access(1337, 1337, 0, nullptr);

    BOOST_TEST(!cx->try_access(0));
    BOOST_TEST(cx->try_access(1));
    BOOST_TEST(cx->try_access(1337));
}

BOOST_AUTO_TEST_CASE(second_chance_entry_gets_not_envicted_on_full_cache)
{
    using namespace vefs::utils;

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

BOOST_AUTO_TEST_CASE(try_purge_returns_false_if_not_owns_last_reference)
{
    cache_page<cached_value> page;

    page.try_start_replace();

    auto rx = page.finish_replace([](void *p) noexcept->result<cached_value *> {
        return new (p) cached_value(4, 10, nullptr);
    });

    BOOST_TEST(!page.try_purge(false));
}

BOOST_AUTO_TEST_CASE(cancel_replace_kills_page)
{
    cache_page<cached_value> page;

    page.try_start_replace();

    page.cancel_replace();

    BOOST_TEST(page.is_dead());
}


BOOST_AUTO_TEST_CASE(try_purge_returns_false_if_dead)
{
    struct non_trivially_destructable_value
    {
        non_trivially_destructable_value(bool &destructor_called) noexcept
            : destructor_called{destructor_called}
        {
        }
        bool &destructor_called;

        ~non_trivially_destructable_value()
        {
            destructor_called = true;
        }
    };

    cache_page<non_trivially_destructable_value> page;

    page.try_start_replace();

    bool destructor_called = false;

    auto rx = page.finish_replace([&destructor_called](void *p) noexcept->result<non_trivially_destructable_value *> {
        return new (p) non_trivially_destructable_value(destructor_called);
    });

    BOOST_TEST(page.try_purge(true));
    BOOST_TEST(destructor_called);
}




BOOST_AUTO_TEST_SUITE_END()
