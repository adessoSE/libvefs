#include "vefs/detail/tree_walker.hpp"
#include "boost-unit-test.hpp"

#include <limits>

BOOST_AUTO_TEST_SUITE(vefs_tree_walker_tests)

BOOST_AUTO_TEST_CASE(tree_position_adds_position_and_layer)
{
    vefs::detail::tree_position test_subject
            = vefs::detail::tree_position(0xF, 0xf6);

    uint64_t layer = 0xf6;
    BOOST_TEST(test_subject.raw() == (layer << 56 | 0xf));
}

BOOST_AUTO_TEST_CASE(tree_position_only_considers_eight_layer_bits)
{
    vefs::detail::tree_position test_subject
            = vefs::detail::tree_position(0xF, 0xf6e5);

    BOOST_TEST(test_subject.raw() == (0xe50000000000000f));
}

BOOST_AUTO_TEST_CASE(tree_position_only_considers_56_position_bits)
{
    std::uint64_t position_inside_layer = 0x1122334455667788;

    vefs::detail::tree_position test_subject
            = vefs::detail::tree_position(position_inside_layer, 0xf6);

    BOOST_TEST(test_subject.raw() == (0xf622334455667788u));
}

BOOST_AUTO_TEST_CASE(tree_position_init_with_max_value)
{
    vefs::detail::tree_position test_subject = vefs::detail::tree_position();

    BOOST_TEST(test_subject.raw() == 0xffffffffffffffffu);
}

BOOST_AUTO_TEST_CASE(tree_position_sets_new_layer)
{
    vefs::detail::tree_position test_subject
            = vefs::detail::tree_position(0xf, 0xf6);

    test_subject.layer(0xab);

    BOOST_TEST(test_subject.raw() == 0xab0000000000000fu);
}

BOOST_AUTO_TEST_CASE(position_returns_position_potion_of_position)
{
    vefs::detail::tree_position test_subject
            = vefs::detail::tree_position(0xaf, 0xf6);

    auto result = test_subject.position();

    BOOST_TEST(result == 0xafu);
}

BOOST_AUTO_TEST_CASE(tree_position_sets_new_position)
{
    vefs::detail::tree_position test_subject
            = vefs::detail::tree_position(0xf, 0xf6);

    test_subject.position(0xabu);

    BOOST_TEST(test_subject.raw() == 0xf6000000000000abu);
}

BOOST_AUTO_TEST_CASE(parent_returns_position_of_parent)
{
    vefs::detail::tree_position test_subject
            = vefs::detail::tree_position(1023 * 5 + 2, 0xf6);

    auto result = test_subject.parent();

    BOOST_TEST(result.layer() == 0xf7);
    BOOST_TEST(result.position() == 5u);
}

BOOST_AUTO_TEST_CASE(parent_array_offset)
{
    vefs::detail::tree_position test_subject
            = vefs::detail::tree_position(1023 * 5 + 2, 0xf6);

    BOOST_TEST(test_subject.parent_array_offset() == 2);
}

BOOST_AUTO_TEST_CASE(bool_comparison_returns_true_for_equal_positions)
{
    vefs::detail::tree_position test_subject
            = vefs::detail::tree_position(123456, 78);
    vefs::detail::tree_position tree_position_to_compare
            = vefs::detail::tree_position(123456, 78);

    auto result = test_subject == tree_position_to_compare;

    BOOST_TEST(result);
}

BOOST_AUTO_TEST_CASE(bool_comparison_returns_false_for_unequal_positions)
{
    vefs::detail::tree_position test_subject
            = vefs::detail::tree_position(123456, 78);
    vefs::detail::tree_position tree_position_to_compare
            = vefs::detail::tree_position(123456, 79);

    auto result = test_subject != tree_position_to_compare;

    BOOST_TEST(result);
}

BOOST_AUTO_TEST_CASE(tree_path_init_for_depth_and_layer_zero)
{
    vefs::detail::tree_position position = vefs::detail::tree_position(2, 0);
    vefs::detail::tree_path test_subject = vefs::detail::tree_path(0, position);

    BOOST_TEST(test_subject.position(0) == 0u);
    BOOST_TEST(test_subject.offset(0) == 0);
}

BOOST_AUTO_TEST_CASE(tree_path_init_for_depth_and_layer_1)
{
    vefs::detail::tree_position position = vefs::detail::tree_position(2, 1);
    vefs::detail::tree_path test_subject = vefs::detail::tree_path(1, position);

    BOOST_TEST(test_subject.position(1) == 0u);
    BOOST_TEST(test_subject.offset(1) == 0);
}

BOOST_AUTO_TEST_CASE(tree_path_for_depth_1_layer_4_position_9)
{
    vefs::detail::tree_position position = vefs::detail::tree_position(9, 4);
    vefs::detail::tree_path test_subject = vefs::detail::tree_path(5, position);

    BOOST_TEST(test_subject.position(4) == 9u);

    BOOST_TEST(test_subject.offset(4) == 9);
}

BOOST_AUTO_TEST_CASE(tree_path_for_depth_5_layer_2_position_9)
{
    vefs::detail::tree_position position = vefs::detail::tree_position(9, 2);
    vefs::detail::tree_path test_subject = vefs::detail::tree_path(5, position);

    BOOST_TEST(test_subject.position(2) == 9u);
    BOOST_TEST(test_subject.position(3) == 0u);
    BOOST_TEST(test_subject.position(4) == 0u);

    BOOST_TEST(test_subject.offset(2) == 9);
    BOOST_TEST(test_subject.offset(3) == 0);
    BOOST_TEST(test_subject.offset(4) == 0);
}

BOOST_AUTO_TEST_CASE(iterator_test_begin)
{
    vefs::detail::tree_position position = vefs::detail::tree_position(9, 2);
    vefs::detail::tree_path test_subject = vefs::detail::tree_path(5, position);

    auto iter = test_subject.begin();

    BOOST_TEST(5 == iter->layer());
    BOOST_TEST(0u == iter->position());
}

BOOST_AUTO_TEST_CASE(iterator_test_next_in_the_middle_of_path)
{
    vefs::detail::tree_position position = vefs::detail::tree_position(9, 2);
    vefs::detail::tree_path test_subject = vefs::detail::tree_path(3, position);

    auto path = test_subject.next();

    BOOST_TEST(0u == path.position(3));
    BOOST_TEST(10u == path.position(2));
}

BOOST_AUTO_TEST_CASE(iterator_test_previous_in_the_middle_of_path)
{
    vefs::detail::tree_position position = vefs::detail::tree_position(9, 2);
    vefs::detail::tree_path test_subject = vefs::detail::tree_path(3, position);

    auto path = test_subject.previous();

    BOOST_TEST(0u == path.position(3));
    BOOST_TEST(8u == path.position(2));
}

BOOST_AUTO_TEST_CASE(iterator_test_previous_in_beginning_of_layer)
{
    vefs::detail::tree_position position = vefs::detail::tree_position(0, 2);
    vefs::detail::tree_path test_subject = vefs::detail::tree_path(3, position);

    auto path = test_subject.previous();

    BOOST_TEST(0u == path.position(3));
    BOOST_TEST(0xffffffffffffffffu == path.position(2));
}

BOOST_AUTO_TEST_CASE(iterator_test_end)
{
    vefs::detail::tree_position position = vefs::detail::tree_position(0, 2);
    vefs::detail::tree_path test_subject = vefs::detail::tree_path(3, position);

    auto path_iter = test_subject.end();
    BOOST_TEST(1 == path_iter->layer());
}

BOOST_AUTO_TEST_SUITE_END()
