#include "vefs/cache/slru_policy.hpp"

#include "vefs/cache/eviction_policy.hpp"

#include "boost-unit-test.hpp"

using namespace vefs::detail;

template class vefs::detail::segmented_least_recently_used_policy<uint64_t,
                                                                  uint16_t>;

static_assert(eviction_policy<
              segmented_least_recently_used_policy<uint64_t, uint16_t>>);
static_assert(
        std::regular<segmented_least_recently_used_policy<uint64_t, uint16_t>::
                             replacement_iterator>);
namespace vefs_tests
{

namespace slru_policy
{

using test_key = uint64_t;
using test_index = uint16_t;

using test_policy = segmented_least_recently_used_policy<test_key, test_index>;

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

struct get_page_key
{
    template <typename K>
    auto operator()(cache_page_state<K> const &state) const noexcept
            -> K const &
    {
        return state.key();
    }
};

} // namespace slru_policy

BOOST_FIXTURE_TEST_SUITE(slru_policy, slru_policy::fixture)

BOOST_AUTO_TEST_CASE(ctor_with_pages)
{
    BOOST_TEST(subject.num_managed() == 0U);
}

BOOST_AUTO_TEST_CASE(insert_one)
{
    test_policy::page_state::state_type gen;
    test_key key = 0xdead'beef;
    test_index idx = 1U;

    BOOST_TEST_REQUIRE((pages[idx].try_start_replace(gen)
                        == cache_replacement_result::dead));
    pages[idx].finish_replace(key);

    subject.insert(key, idx);

    BOOST_TEST(subject.num_managed() == 1U);
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

BOOST_FIXTURE_TEST_CASE(newly_inserted_are_due_before_protected, with_elements)
{
    test_key protectedKey = 0U;
    test_key newElementKey = 0xdead'beef;
    test_index newElementIndex = 32U;
    BOOST_TEST(subject.on_access(protectedKey, 0U));
    {
        test_policy::page_state::state_type gen;
        (void)pages[newElementIndex].try_start_replace(gen);
        pages[newElementIndex].finish_replace(newElementKey);
        pages[newElementIndex].release();
        subject.insert(newElementKey, newElementIndex);
    }

    auto const begin = subject.begin();
    auto const end = subject.end();
    BOOST_TEST(
            std::ranges::distance(
                    std::ranges::find(begin, end, newElementKey,
                                      get_page_key{}),
                    std::ranges::find(begin, end, protectedKey, get_page_key{}))
            == 1);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace vefs_tests
