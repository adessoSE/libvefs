#include "vefs/cache/bloom_filter.hpp"

#include <algorithm>
#include <numeric>

#include "boost-unit-test.hpp"
#include "test-utils.hpp"

namespace vefs_tests::bloom
{

struct triv_h
{
    vefs::hash128_t hash;

    constexpr triv_h(std::uint32_t h1,
                     std::uint32_t h2,
                     std::uint32_t h3,
                     std::uint32_t h4)
        : hash{h1 | static_cast<std::uint64_t>(h2) << 32,
               h3 | static_cast<std::uint64_t>(h4) << 32}
    {
    }

    template <vefs::hash_algorithm Algorithm>
    friend inline void
    tag_invoke(vefs::hash_update_fn, Algorithm &hashState, triv_h v) noexcept
    {
        hashState.update(reinterpret_cast<std::byte const *>(&v.hash),
                         sizeof(v.hash));
    }

    template <vefs::hash_algorithm Algorithm, dplx::cncr::unsigned_integer H>
    friend inline auto tag_invoke(vefs::hash_fn<Algorithm, H>,
                                  triv_h v) noexcept -> H
    {
        return Algorithm::template hash<H>(
                reinterpret_cast<std::byte const *>(&v.hash), sizeof(v.hash));
    }
    template <vefs::hash_algorithm Algorithm>
    friend inline auto tag_invoke(vefs::hash_fn<Algorithm, vefs::hash128_t>,
                                  triv_h v) noexcept -> vefs::hash128_t
    {
        return v.hash;
    }
};

} // namespace vefs_tests::bloom

template <>
inline constexpr bool
        vefs::disable_trivially_hashable<vefs_tests::bloom::triv_h> = true;

namespace vefs_tests
{

BOOST_AUTO_TEST_SUITE(bloom)

inline constexpr std::uint32_t cells = 1024U;
inline constexpr std::uint32_t divider = 0x1'0000'0000U / cells;

using test_type = vefs::detail::bloom_filter<int>;
static_assert(std::semiregular<test_type>);

BOOST_AUTO_TEST_CASE(default_ctor)
{
    test_type subject;
    BOOST_TEST(subject.num_cells() == 0U);
}

BOOST_AUTO_TEST_CASE(allocating_ctor)
{
    test_type subject(1024U);
    BOOST_TEST(subject.num_cells() == 1024U);
}

BOOST_AUTO_TEST_CASE(copy_ctor)
{
    constexpr test_type::value_type item = 1;
    test_type subject(1024U);
    subject.observe(item);
    BOOST_TEST(subject.estimate(item) == 1U);

    test_type copy(subject);
    BOOST_TEST(copy.num_cells() == subject.num_cells());
    BOOST_TEST(copy.estimate(item) == 1U);
    BOOST_TEST(subject.estimate(item) == 1U);
}
BOOST_AUTO_TEST_CASE(copy_assigment)
{
    constexpr test_type::value_type item = 1;
    test_type subject(1024U);
    subject.observe(item);
    BOOST_TEST(subject.estimate(item) == 1U);

    test_type copy;
    copy = subject;
    BOOST_TEST(copy.num_cells() == subject.num_cells());
    BOOST_TEST(copy.estimate(item) == 1U);
    BOOST_TEST(subject.estimate(item) == 1U);
}

BOOST_AUTO_TEST_CASE(move_ctor)
{
    constexpr test_type::value_type item = 1;
    test_type subject(1024U);
    BOOST_TEST(subject.num_cells() == 1024U);
    subject.observe(item);
    BOOST_TEST(subject.estimate(item) == 1U);

    test_type copy(std::move(subject));
    BOOST_TEST(copy.num_cells() == 1024U);
    BOOST_TEST(copy.estimate(item) == 1U);
    BOOST_TEST(subject.num_cells() == 0U);
}
BOOST_AUTO_TEST_CASE(move_assigment)
{
    constexpr test_type::value_type item = 1;
    test_type subject(1024U);
    BOOST_TEST(subject.num_cells() == 1024U);
    subject.observe(item);
    BOOST_TEST(subject.estimate(item) == 1U);

    test_type copy;
    copy = std::move(subject);
    BOOST_TEST(copy.num_cells() == 1024U);
    BOOST_TEST(copy.estimate(item) == 1U);
    BOOST_TEST(subject.num_cells() == 0U);
}

BOOST_AUTO_TEST_CASE(observe)
{
    constexpr test_type::value_type item = 1;
    test_type subject(1024U);
    subject.observe(item);
    BOOST_TEST(subject.estimate(item) == 1U);
}

BOOST_AUTO_TEST_CASE(observe_distinct)
{
    vefs::detail::bloom_filter<triv_h> subject(cells);

    triv_h v1(1U * divider, 2U * divider, 3U * divider, 4U * divider);
    triv_h v2(5U * divider, 2U * divider, 3U * divider, 4U * divider);
    triv_h v3(5U * divider, 2U * divider, 8U * divider, 4U * divider);

    subject.observe(v1);
    subject.observe(v2);
    subject.observe(v2);

    BOOST_TEST(subject.estimate(v1) == 1U);
    BOOST_TEST(subject.estimate(v2) == 1U);
    BOOST_TEST(subject.estimate(v3) == 0U);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace vefs_tests
