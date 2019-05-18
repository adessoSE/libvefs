#include <vefs/detail/raw_archive.hpp>
#include "boost-unit-test.hpp"

#include <random>

namespace bdata = boost::unit_test::data;

static bool check_mult_overflow(std::uint64_t a, std::uint64_t b)
{
    auto x = a * b;
    return a != 0 && x / a != b;
}



BOOST_AUTO_TEST_SUITE(vefs_raw_archive_tests)

BOOST_AUTO_TEST_SUITE_END()
