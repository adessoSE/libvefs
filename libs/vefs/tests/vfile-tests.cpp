#include "../src/vfile.hpp"
#include "../src/vfilesystem.hpp"
#include "boost-unit-test.hpp"
#include <vefs/disappointment.hpp>

#include "test-utils.hpp"

using namespace vefs;
using namespace vefs::detail;

struct vfile_dependencies_fixture
{
    static constexpr std::array<std::byte, 32> default_user_prk = {};

    master_file_info filesystemIndex;
    llfio::mapped_file_handle testFile;
    std::unique_ptr<sector_device> device;
    std::unique_ptr<file_crypto_ctx> cryptoCtx;

    archive_sector_allocator sectorAllocator;
    std::unique_ptr<vfilesystem> fileSystem;
    std::shared_ptr<vfile> testSubject;

    pooled_work_tracker workExecutor;

    vfile_dependencies_fixture()
        : testFile(vefs::llfio::mapped_temp_inode().value())
        , device(sector_device::create_new(testFile.reopen(0).value(),
                                           test::only_mac_crypto_provider(),
                                           default_user_prk)
                         .value()
                         .device)
        , filesystemIndex{}
        , sectorAllocator(*device, {})
        , workExecutor(&thread_pool::shared())
    {
        TEST_RESULT_REQUIRE(sectorAllocator.initialize_new());

        cryptoCtx = device->create_file_secrets().value();

        fileSystem = vefs::vfilesystem::create_new(*device, sectorAllocator,
                                                workExecutor, filesystemIndex)
                          .value();

        testSubject = vefs::vfile::create_new(
                              fileSystem.get(), workExecutor, sectorAllocator,
                              file_id(vefs::utils::uuid{
                                      0xc7, 0xa5, 0x3d, 0x7a, 0xa4, 0xf0, 0x40,
                                      0x53, 0xa7, 0xa3, 0x35, 0xf3, 0x5c, 0xdf,
                                      0x53, 0x3d}),
                              *device, *cryptoCtx)
                              .value();
    }
};

BOOST_FIXTURE_TEST_SUITE(vfile_tests, vfile_dependencies_fixture)

BOOST_AUTO_TEST_CASE(new_vfile_is_dirty)
{
    BOOST_TEST(testSubject->is_dirty());
    BOOST_TEST(testSubject->maximum_extent() == 0);
}

BOOST_AUTO_TEST_CASE(read_from_empty_file)
{
    auto result = utils::make_byte_array(0x0, 0x0, 0x0, 0x0);

    TEST_RESULT_REQUIRE(testSubject->read(result, 10));

    BOOST_TEST(result == utils::make_byte_array(0x0, 0x0, 0x0, 0x0));
}

BOOST_AUTO_TEST_CASE(write_1_bytes_at_pos_0_creates_max_extend_1)
{
    auto result = utils::make_byte_array(0x9);

    auto write_rx = testSubject->write(result, 0);
    TEST_RESULT_REQUIRE(write_rx);
    BOOST_TEST(testSubject->maximum_extent() == 1);
}

BOOST_AUTO_TEST_CASE(write_4_bytes_at_pos_5_creates_max_extend_9)
{
    auto result = utils::make_byte_array(0x9, 0x22, 0x6, 0xde);

    auto write_rx = testSubject->write(result, 5);
    TEST_RESULT_REQUIRE(write_rx);
    BOOST_TEST(testSubject->maximum_extent() == 9);
}

BOOST_AUTO_TEST_CASE(write_4_bytes_at_pos_5_and_read_it)
{
    auto writeBlob = utils::make_byte_array(0x9, 0x22, 0x6, 0xde);

    auto write_rx = testSubject->write(writeBlob, 5);
    TEST_RESULT_REQUIRE(write_rx);
    auto result = utils::make_byte_array(0x0, 0x0, 0x0, 0x0);
    auto read_rx = testSubject->read(result, 5);
    TEST_RESULT_REQUIRE(read_rx);

    BOOST_TEST(result == writeBlob);
}

BOOST_AUTO_TEST_CASE(write_4_bytes_at_pos_5_and_read_from_pos_3)
{
    auto writeBlob = utils::make_byte_array(0x9, 0x22, 0x6, 0xde);

    auto write_rx = testSubject->write(writeBlob, 5);
    TEST_RESULT_REQUIRE(write_rx);
    auto result = utils::make_byte_array(0x0, 0x0, 0x0, 0x0);
    auto read_rx = testSubject->read(result, 4);
    TEST_RESULT_REQUIRE(read_rx);

    BOOST_TEST(result == utils::make_byte_array(0x0, 0x9, 0x22, 0x6));
}

BOOST_AUTO_TEST_CASE(write_4_bytes_at_pos_5_wo_truncation_and_read_from_pos_3)
{
    TEST_RESULT_REQUIRE(testSubject->truncate(20));

    auto writeBlob = utils::make_byte_array(0x9, 0x22, 0x6, 0xde);

    auto write_rx = testSubject->write(writeBlob, 5);
    TEST_RESULT_REQUIRE(write_rx);

    auto result = utils::make_byte_array(0x0, 0x0, 0x0, 0x0);
    auto read_rx = testSubject->read(result, 4);
    TEST_RESULT_REQUIRE(read_rx);

    BOOST_TEST(result == utils::make_byte_array(0x0, 0x9, 0x22, 0x6));
}

BOOST_AUTO_TEST_CASE(decrease_size_from_9_to_3)
{
    auto writeBlob = utils::make_byte_array(0x9, 0x22, 0x6, 0xde);

    auto write_rx = testSubject->write(writeBlob, 5);
    TEST_RESULT_REQUIRE(write_rx);

    TEST_RESULT_REQUIRE(testSubject->truncate(3));

    BOOST_TEST(testSubject->maximum_extent() == 3);
}

BOOST_AUTO_TEST_SUITE_END()
