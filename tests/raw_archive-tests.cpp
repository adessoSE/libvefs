#include <vefs/detail/raw_archive.hpp>
#include "boost-unit-test.hpp"

#include <random>

#include <vefs/detail/file_sector.hpp>
#include <vefs/utils/random.hpp>

namespace bdata = boost::unit_test::data;

struct test_rng : vefs::utils::xoroshiro128plus
{
    test_rng()
        : xoroshiro128plus(0, 0)
    {
    }
    using xoroshiro128plus::xoroshiro128plus;
};

static bool check_mult_overflow(std::uint64_t a, std::uint64_t b)
{
    auto x = a * b;
    return a != 0 && x / a != b;
}



BOOST_AUTO_TEST_SUITE(vefs_raw_archive_tests)

BOOST_AUTO_TEST_SUITE_END()
