#include "../src/vfilesystem.hpp"

#include "boost-unit-test.hpp"
#include "test-utils.hpp"

#include "../src/detail/sector_device.hpp"

using namespace vefs;
using namespace vefs::detail;

struct vfilesystem_pre_create_fixture
{
    static constexpr std::array<std::byte, 32> default_user_prk = {};

    llfio::mapped_file_handle testFile;
    std::unique_ptr<sector_device> device;
    pooled_work_tracker workExecutor;

    archive_sector_allocator sectorAllocator;

    vfilesystem_pre_create_fixture()
        : testFile(vefs::llfio::mapped_temp_inode().value())
        , device(sector_device::open(testFile.reopen(0).value(),
                                     crypto::debug_crypto_provider(),
                                     default_user_prk, true)
                     .value())
        , workExecutor(&thread_pool::shared())
        , sectorAllocator(*device)
    {
    }
};

BOOST_FIXTURE_TEST_SUITE(vfilesystem_tests, vfilesystem_pre_create_fixture)

BOOST_AUTO_TEST_CASE(recover_sectors_simple)
{
    TEST_RESULT_REQUIRE(sectorAllocator.initialize_new());

    auto vfsrx =
        vfilesystem::create_new(*device, sectorAllocator, workExecutor,
                                device->archive_header().filesystem_index);
    TEST_RESULT_REQUIRE(vfsrx);
    auto vfs = std::move(vfsrx).assume_value();

    auto vfilerx = vfs->open("testpath", file_open_mode::readwrite |
                                             file_open_mode::create);
    TEST_RESULT_REQUIRE(vfilerx);
    TEST_RESULT_REQUIRE(vfilerx.assume_value()->truncate(0xFFFF));
    TEST_RESULT_REQUIRE(vfilerx.assume_value()->commit());
    vfilerx.assume_value() = nullptr;

    TEST_RESULT_REQUIRE(vfs->commit());

    vfs.reset();
    std::destroy_at(&sectorAllocator);
    new (&sectorAllocator) archive_sector_allocator(*device);

    vfsrx =
        vfilesystem::open_existing(*device, sectorAllocator, workExecutor,
                                   device->archive_header().filesystem_index);
    TEST_RESULT_REQUIRE(vfsrx);
    vfs = std::move(vfsrx).assume_value();

    auto xsize = device->size();

    TEST_RESULT_REQUIRE(vfs->recover_unused_sectors());
    TEST_RESULT_REQUIRE(sectorAllocator.initialize_new());

    TEST_RESULT_REQUIRE(sectorAllocator.alloc_one());
    TEST_RESULT_REQUIRE(sectorAllocator.alloc_one());

    BOOST_TEST(xsize == device->size());
}

BOOST_AUTO_TEST_SUITE_END()
