#include "vefs/hash/spooky_v2.hpp"

#include <vefs/hash/hash_algorithm.hpp>

#include "boost-unit-test.hpp"
#include "test-utils.hpp"

using namespace vefs;
using namespace vefs::detail;

namespace bdata = boost::unit_test::data;

BOOST_AUTO_TEST_SUITE(hash_tests)

static_assert(hash_algorithm<spooky_v2_hash>);
static_assert(keyable_hash_algorithm<spooky_v2_hash>);

BOOST_AUTO_TEST_CASE(cmp)
{
    int value = 3;
    spooky_v2_hash state;
    hash_update(state, 3);
    (void)hash<spooky_v2_hash, std::uint32_t>(spooky_v2_hash::key_type{},
                                              value);
}

BOOST_AUTO_TEST_SUITE_END()
