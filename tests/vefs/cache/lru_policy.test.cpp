#include "vefs/cache/lru_policy.hpp"

#include "vefs/cache/eviction_policy.hpp"

#include "boost-unit-test.hpp"

using namespace vefs::detail;

template class vefs::detail::least_recently_used_policy<uint64_t, uint16_t>;

static_assert(eviction_policy<least_recently_used_policy<uint64_t, uint16_t>>);
static_assert(std::regular<least_recently_used_policy<uint64_t, uint16_t>::
                                   replacement_iterator>);

namespace vefs_tests
{

namespace lru_policy
{

using test_key = uint64_t;
using test_index = uint16_t;

using test_policy = least_recently_used_policy<test_key, test_index>;

using test_pages = std::vector<test_policy::page_state>;

struct fixture
{
    test_pages pages;
    test_policy subject;

    fixture()
        : pages(64)
        , subject(pages, pages.size())
    {
    }
};

struct with_elements : fixture
{
    with_elements()
        : fixture()
    {
        test_policy::page_state::state_type gen;
        for (std::uint16_t i = 0U; i < 4; ++i)
        {
            (void)pages[i].try_start_replace(gen);
            pages[i].finish_replace(i);
            pages[i].release();
            subject.insert(i, i);
        }
    }
};

} // namespace lru_policy

BOOST_FIXTURE_TEST_SUITE(lru_policy, lru_policy::fixture)

BOOST_AUTO_TEST_CASE(ctor_with_pages)
{
    BOOST_TEST(subject.num_managed() == 0U);
}

BOOST_AUTO_TEST_CASE(insert_one)
{
    test_policy::page_state::state_type gen;
    test_key key = 0xdeadbeef;
    test_index idx = 1U;

    BOOST_TEST_REQUIRE((pages[idx].try_start_replace(gen)
                        == cache_replacement_result::dead));
    pages[idx].finish_replace(key);

    subject.insert(key, idx);

    BOOST_TEST(subject.num_managed() == 1U);
    // page one is still pinned
    BOOST_TEST(std::distance(subject.begin(), subject.end()) == 0);
    pages[idx].release();
    BOOST_TEST_REQUIRE(std::distance(subject.begin(), subject.end()) == 1);
    BOOST_TEST(subject.begin()->key() == key);
}

BOOST_FIXTURE_TEST_CASE(move_to_back_on_access, with_elements)
{
    BOOST_TEST(subject.begin()->key() == 0U);
    BOOST_TEST(subject.on_access(0U, 0U));
    BOOST_TEST(subject.begin()->key() == 1U);
    BOOST_TEST(std::next(subject.begin(), 3)->key() == 0U);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace vefs_tests
