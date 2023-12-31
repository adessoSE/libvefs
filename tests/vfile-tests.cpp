#include "vefs/vfile.hpp"
#include "vefs/vfilesystem.hpp"

#include <vefs/disappointment.hpp>
#include "boost-unit-test.hpp"

#include "test-utils.hpp"

using namespace vefs;
using namespace vefs::detail;

struct vfile_dependencies_fixture
{
    static constexpr std::array<std::byte, 32> default_user_prk = {};

    master_file_info filesystemIndex;
    llfio::file_handle testFile;
    std::unique_ptr<sector_device> device;
    std::unique_ptr<file_crypto_ctx> cryptoCtx;

    archive_sector_allocator sectorAllocator;
    std::unique_ptr<vfilesystem> fileSystem;
    std::shared_ptr<vfile> testSubject;

    pooled_work_tracker workExecutor;

    vfile_dependencies_fixture()
        : filesystemIndex{}
        , testFile(vefs::llfio::temp_inode().value())
        , device(sector_device::create_new(testFile.reopen().value(),
                                           test::only_mac_crypto_provider(),
                                           default_user_prk)
                         .value()
                         .device)
        , sectorAllocator(*device, {})
        , workExecutor(&thread_pool::shared())
    {
        TEST_RESULT_REQUIRE(sectorAllocator.initialize_new());

        cryptoCtx = device->create_file_secrets().value();

        fileSystem
                = vefs::vfilesystem::create_new(*device, sectorAllocator,
                                                workExecutor, filesystemIndex)
                          .value();

        testSubject = fileSystem
                              ->open("test-file",
                                     vefs::file_open_mode::create
                                             | vefs::file_open_mode::readwrite)
                              .value();
    }

    ~vfile_dependencies_fixture()
    {
        if (testSubject->is_dirty())
        {
            (void)testSubject->commit();
            (void)fileSystem->commit();
        }
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

BOOST_AUTO_TEST_CASE(extract_to_file)
{
    // given
    llfio::file_handle fileHandle{llfio::temp_inode().value()};
    auto writeBlob = utils::make_byte_array(0x41, 0x42, 0x43, 0x44);
    auto write_rx = testSubject->write(writeBlob, 0);
    TEST_RESULT_REQUIRE(write_rx);

    // when
    TEST_RESULT_REQUIRE(testSubject->extract(fileHandle));

    // then
    // create read request
    auto resultBuffer = std::make_unique<std::byte[]>(writeBlob.size());
    llfio::byte_io_handle::buffer_type buffers[1] = {
            {resultBuffer.get(), writeBlob.size()}
    };
    auto result = fileHandle.read({buffers, 0});

    BOOST_TEST(result.bytes_transferred() == 4);
    BOOST_TEST(result.value()[0] == writeBlob,
               boost::test_tools::per_element{});
}

BOOST_AUTO_TEST_SUITE_END()
