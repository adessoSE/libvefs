#include "boost-unit-test.hpp"
#include <vefs/detail/tree_lut.hpp>

BOOST_AUTO_TEST_SUITE(vefs_tree_lut_tests)

BOOST_AUTO_TEST_CASE(init_step_is_one)
{
    auto width = vefs::detail::lut::detail::compute_step_width_lut();

    BOOST_TEST(width[0] == 1u);
}

BOOST_AUTO_TEST_CASE(tree_ref_width_lut)
{
    auto width = vefs::detail::lut::detail::compute_ref_width_lut();

    BOOST_TEST(width[0] == 1u);
}

BOOST_AUTO_TEST_CASE(calculate_sector_pos_for_byte_in_sector_five)
{
    auto sector_position = vefs::detail::lut::sector_position_of(5 * 32702);

    BOOST_TEST(sector_position == 4u);
}

BOOST_AUTO_TEST_CASE(calculate_sector_pos_for_byte_in_sector_three)
{
    auto sector_position = vefs::detail::lut::sector_position_of(3 * 32702);

    BOOST_TEST(sector_position == 2u);
}

BOOST_AUTO_TEST_CASE(required_tree_depth_for_sector_pos_5_is_1)
{
    auto required_tree_depth = vefs::detail::lut::required_tree_depth(5);

    BOOST_TEST(required_tree_depth == 1);
}

BOOST_AUTO_TEST_CASE(required_tree_depth_for_pos_2_to_the_exp_of_14_is_2)
{
    uint64_t pos = 1;
    pos <<= 14; //2^14
    auto required_tree_depth = vefs::detail::lut::required_tree_depth(pos);

    BOOST_TEST(required_tree_depth == 2);
}

BOOST_AUTO_TEST_CASE(required_tree_depth_for_pos_2_to_the_exp_of_29_is_3)
{
    uint64_t pos = 1;
    pos <<= 29; //2^29
    auto required_tree_depth = vefs::detail::lut::required_tree_depth(pos);

    BOOST_TEST(required_tree_depth == 3);
}

BOOST_AUTO_TEST_CASE(required_tree_depth_for_pos_2_to_the_exp_of_39_is_4)
{
    uint64_t pos = 1;
    pos <<= 39; // 2 ^ 39
    auto required_tree_depth = vefs::detail::lut::required_tree_depth(pos);

    BOOST_TEST(required_tree_depth == 4);
}

BOOST_AUTO_TEST_CASE(required_tree_depth_for_pos_2_to_the_exp_of_40_is_5)
{
    uint64_t pos = 1;
    pos <<= 40; //2^40
    auto required_tree_depth = vefs::detail::lut::required_tree_depth(pos);

    BOOST_TEST(required_tree_depth == 5);
}

BOOST_AUTO_TEST_SUITE_END()
