#include "vefs/cache/cache_page.hpp"

#include "boost-unit-test.hpp"

using namespace vefs::detail;
using namespace vefs::detail::cache_ng;

namespace vefs_tests
{

BOOST_AUTO_TEST_SUITE(cache_page)

using test_page_state = cache_ng::cache_page_state<std::uint16_t>;
using page_state_ptr = dplx::cncr::intrusive_ptr<test_page_state>;

static_assert(std::is_default_constructible_v<test_page_state>);
static_assert(!std::is_move_constructible_v<test_page_state>);
static_assert(!std::is_move_assignable_v<test_page_state>);

static_assert(dplx::cncr::ref_counted<test_page_state>);
static_assert(dplx::cncr::ref_counted<test_page_state const>);

BOOST_AUTO_TEST_CASE(dead_on_construction)
{
    test_page_state subject;

    BOOST_TEST(subject.is_dead());
    BOOST_TEST(!subject.is_dirty());
    BOOST_TEST(!subject.try_acquire(0U, 0U));
}

BOOST_AUTO_TEST_CASE(initialize_dead)
{
    test_page_state::state_type gen;
    test_page_state subject;

    BOOST_TEST_REQUIRE(
            (subject.try_start_replace(gen) == cache_replacement_result::dead));
    BOOST_TEST(!subject.is_dead());
    BOOST_TEST(subject.is_dirty());
    BOOST_TEST(gen == 0x0004'0000U);
    subject.finish_replace(0xacdcU);
    BOOST_TEST(!subject.is_dead());
    BOOST_TEST(!subject.is_dirty());
    BOOST_TEST(subject.is_pinned());
    BOOST_TEST(subject.key() == 0xacdcU);
}

BOOST_AUTO_TEST_CASE(replace_clean)
{
    test_page_state::state_type gen;
    test_page_state subject;
    BOOST_TEST_REQUIRE(
            (subject.try_start_replace(gen) == cache_replacement_result::dead));
    BOOST_TEST_REQUIRE(gen == 0x0004'0000U);
    subject.finish_replace(0xacdcU);
    subject.release();

    BOOST_TEST_REQUIRE((subject.try_start_replace(gen)
                        == cache_replacement_result::clean));
    BOOST_TEST(!subject.is_dead());
    BOOST_TEST(subject.is_dirty());
    BOOST_TEST(gen == 0x0008'0000U);
    subject.finish_replace(0xacddU);
    BOOST_TEST(!subject.is_dead());
    BOOST_TEST(!subject.is_dirty());
    BOOST_TEST(subject.is_pinned());
    BOOST_TEST(subject.key() == 0xacddU);
}

BOOST_AUTO_TEST_CASE(replace_dirty)
{
    test_page_state::state_type gen;
    test_page_state subject;
    BOOST_TEST_REQUIRE(
            (subject.try_start_replace(gen) == cache_replacement_result::dead));
    BOOST_TEST_REQUIRE(gen == 0x0004'0000U);
    subject.finish_replace(0xacdcU);
    subject.mark_dirty();
    subject.release();

    BOOST_TEST_REQUIRE((subject.try_start_replace(gen)
                        == cache_replacement_result::dirty));
    BOOST_TEST(!subject.is_dead());
    BOOST_TEST(subject.is_dirty());
    BOOST_TEST(gen == 0x0008'0000U);
    subject.update_generation();
    subject.finish_replace(0xacddU);
    BOOST_TEST(!subject.is_dead());
    BOOST_TEST(!subject.is_dirty());
    BOOST_TEST(subject.is_pinned());
    BOOST_TEST(subject.key() == 0xacddU);
}

BOOST_AUTO_TEST_CASE(prevent_pinned_replacement)
{
    test_page_state::state_type gen;
    test_page_state subject;
    BOOST_TEST_REQUIRE(
            (subject.try_start_replace(gen) == cache_replacement_result::dead));
    BOOST_TEST_REQUIRE(gen == 0x0004'0000U);
    subject.finish_replace(0xacdcU);

    BOOST_TEST_REQUIRE((subject.try_start_replace(gen)
                        == cache_replacement_result::pinned));
    BOOST_TEST(gen == 0x0004'0000U);
    BOOST_TEST(!subject.is_dead());
    BOOST_TEST(!subject.is_dirty());
    BOOST_TEST(subject.key() == 0xacdcU);
}

BOOST_AUTO_TEST_CASE(cancel_replacement)
{
    test_page_state::state_type gen;
    test_page_state subject;
    BOOST_TEST_REQUIRE(
            (subject.try_start_replace(gen) == cache_replacement_result::dead));
    BOOST_TEST_REQUIRE(gen == 0x0004'0000U);
    subject.finish_replace(0xacdcU);
    subject.release();

    BOOST_TEST_REQUIRE((subject.try_start_replace(gen)
                        == cache_replacement_result::clean));
    BOOST_TEST(gen == 0x0008'0000U);
    subject.cancel_replace();
    BOOST_TEST(subject.is_dead());
}

BOOST_AUTO_TEST_CASE(can_be_managed_with_intrusive_ptr)
{
    test_page_state::state_type gen;
    test_page_state subject;
    BOOST_TEST_REQUIRE(
            (subject.try_start_replace(gen) == cache_replacement_result::dead));
    BOOST_TEST_REQUIRE(gen == 0x0004'0000U);
    subject.finish_replace(0xacdcU);

    {
        page_state_ptr ptr = dplx::cncr::intrusive_ptr_acquire(&subject);
        subject.release();
        BOOST_TEST(subject.is_pinned());
    }
    BOOST_TEST(!subject.is_pinned());
    BOOST_TEST(!subject.is_dead());
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace vefs_tests
