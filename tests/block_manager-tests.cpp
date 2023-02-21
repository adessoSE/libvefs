#include "vefs/detail/block_manager.hpp"

#include "boost-unit-test.hpp"
#include "test-utils.hpp"

struct block_manager_fixture
{
    vefs::utils::block_manager<std::uint64_t> test_subject;
    block_manager_fixture()
    {
    }
};

BOOST_FIXTURE_TEST_SUITE(block_manager_tests, block_manager_fixture)

BOOST_AUTO_TEST_CASE(initial_blockmanager_is_all_full)
{
    auto result = test_subject.alloc_one();

    BOOST_TEST(result.has_error());
    BOOST_TEST(result.error() == vefs::archive_errc::resource_exhausted);
}

BOOST_AUTO_TEST_CASE(initial_blockmanager_deallocates_frees_a_block)
{
    (void)test_subject.dealloc_one(5);

    auto result = test_subject.alloc_one();

    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value() == 5);
}

BOOST_AUTO_TEST_CASE(initial_blockmanager_deallocates_contigouus_frees_a_block)
{
    (void)test_subject.dealloc_contiguous(5, 20);

    auto result = test_subject.alloc_one();

    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value() == 5);
}

BOOST_AUTO_TEST_CASE(alloc_contigouus_returns_first_free_id)
{
    (void)test_subject.dealloc_contiguous(5, 20);

    auto result = test_subject.alloc_contiguous(6);

    BOOST_TEST(!result.has_error());
    BOOST_TEST(result.value() == 5);
}

BOOST_AUTO_TEST_CASE(extends_returns_first_block_id)
{
    (void)test_subject.dealloc_contiguous(5, 20);

    auto extend_result = test_subject.extend(3, 4, 1);

    BOOST_TEST(!extend_result.has_error());
    BOOST_TEST(extend_result.value() == 3);
}

BOOST_AUTO_TEST_CASE(
        alloc_contiguous_returns_resource_exhausted_error_if_no_blocks_free)
{
    auto result = test_subject.alloc_contiguous(6);

    BOOST_TEST(result.has_error());
    BOOST_TEST(result.error() == vefs::archive_errc::resource_exhausted);
}

BOOST_AUTO_TEST_CASE(write_zero_to_bitset_does_not_change_anything)
{

    (void)test_subject.dealloc_contiguous(0, 20);

    auto serializedDataStorage = vefs::utils::make_byte_array(
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);

    std::span serializedData{serializedDataStorage};
    vefs::utils::bitset_overlay allocMap{serializedData};

    test_subject.write_to_bitset(allocMap, 0, 0);

    auto resultBitset = vefs::utils::make_byte_array(0xFF, 0xFF, 0xFF, 0xFF,
                                                     0xFF, 0xFF, 0xFF, 0xFF);

    BOOST_CHECK_EQUAL_COLLECTIONS(serializedDataStorage.begin(),
                                  serializedDataStorage.end(),
                                  resultBitset.begin(), resultBitset.end());
}

BOOST_AUTO_TEST_CASE(write_to_bitset_zeros_all_empty_blocks_indizes)
{

    (void)test_subject.dealloc_contiguous(0, 20);

    auto serializedDataStorage = vefs::utils::make_byte_array(
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);

    std::span serializedData{serializedDataStorage};
    vefs::utils::bitset_overlay allocMap{serializedData};

    test_subject.write_to_bitset(allocMap, 0, 50);

    auto resultBitset = vefs::utils::make_byte_array(0x00, 0x00, 0xF0, 0xFF,
                                                     0xFF, 0xFF, 0xFF, 0xFF);

    BOOST_CHECK_EQUAL_COLLECTIONS(serializedDataStorage.begin(),
                                  serializedDataStorage.end(),
                                  resultBitset.begin(), resultBitset.end());
}

BOOST_AUTO_TEST_CASE(write_to_bitset_zeros_all_empty_blocks_indizes2)
{

    (void)test_subject.dealloc_contiguous(0, 20);

    auto serializedDataStorage = vefs::utils::make_byte_array(
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);

    std::span serializedData{serializedDataStorage};
    vefs::utils::bitset_overlay allocMap{serializedData};

    test_subject.write_to_bitset(allocMap, 0, 10);

    auto resultBitset = vefs::utils::make_byte_array(0x00, 0xF8, 0xFF, 0xFF,
                                                     0xFF, 0xFF, 0xFF, 0xFF);

    BOOST_CHECK_EQUAL_COLLECTIONS(serializedDataStorage.begin(),
                                  serializedDataStorage.end(),
                                  resultBitset.begin(), resultBitset.end());
}

BOOST_AUTO_TEST_CASE(write_to_bitset_sets_all_bits_for_used_blocks)
{
    (void)test_subject.dealloc_contiguous(0, 20);
    (void)test_subject.dealloc_contiguous(29, 11);
    auto serializedDataStorage = vefs::utils::make_byte_array(
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

    std::span serializedData{serializedDataStorage};
    vefs::utils::bitset_overlay allocMap{serializedData};

    test_subject.write_to_bitset(allocMap, 0, 50);

    auto resultBitset = vefs::utils::make_byte_array(0x00, 0x00, 0xF0, 0x1F,
                                                     0x00, 0xFF, 0x03, 0x00);

    BOOST_CHECK_EQUAL_COLLECTIONS(serializedDataStorage.begin(),
                                  serializedDataStorage.end(),
                                  resultBitset.begin(), resultBitset.end());
}

BOOST_AUTO_TEST_CASE(parse_to_bitset_configures_deallocates_zero_bits)
{
    auto serializedDataStorage = vefs::utils::make_byte_array(
            0x00, 0x00, 0xF0, 0x1F, 0x00, 0xFF, 0x03, 0x00);

    std::span serializedData{serializedDataStorage};
    vefs::utils::bitset_overlay allocMap{serializedData};

    auto parse_result = test_subject.parse_bitset(allocMap, 0, 50);

    auto alloc_result = test_subject.alloc_contiguous(21);

    BOOST_TEST(!parse_result.has_error());

    BOOST_TEST(alloc_result.has_error());
    BOOST_TEST(alloc_result.error() == vefs::archive_errc::resource_exhausted);
}

BOOST_AUTO_TEST_CASE(clear_block_manager_removes_all_deallocated_blocks)
{
    (void)test_subject.dealloc_contiguous(5, 20);
    test_subject.clear();

    auto result = test_subject.alloc_contiguous(6);

    BOOST_TEST(result.has_error());
    BOOST_TEST(result.error() == vefs::archive_errc::resource_exhausted);
}

BOOST_AUTO_TEST_CASE(merge_nodes_after_end_insertion)
{
    TEST_RESULT_REQUIRE(test_subject.dealloc_one(1));
    TEST_RESULT_REQUIRE(test_subject.dealloc_one(2));
    TEST_RESULT_REQUIRE(test_subject.dealloc_one(3));

    BOOST_TEST(test_subject.num_nodes() == 1);
}

BOOST_AUTO_TEST_SUITE_END()
