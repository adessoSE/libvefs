#include "boost-unit-test.hpp"
#include <vefs/detail/tree_lut.hpp>

BOOST_AUTO_TEST_SUITE(vefs_tree_lut_tests)

BOOST_AUTO_TEST_CASE(tree_step_root_step_is)
{
    auto width = vefs::detail::lut::detail::compute_step_width_lut();

    BOOST_TEST(width[0] == 1);
}

BOOST_AUTO_TEST_CASE(tree_ref_width_lut)
{
    auto width = vefs::detail::lut::detail::compute_ref_width_lut();

    BOOST_TEST(width[0] == 1);
}

BOOST_AUTO_TEST_SUITE_END()
