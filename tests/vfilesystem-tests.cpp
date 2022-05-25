#include "vefs/vfilesystem.hpp"

#include "test-utils.hpp"

#include "vefs/detail/sector_device.hpp"

using namespace vefs;
using namespace vefs::detail;

namespace vefs
{
std::ostream &operator<<(std::ostream &str, file_open_mode_bitset val)
{
    using namespace std::string_view_literals;

    std::array<std::string_view, 4> attributes{};
    auto end = attributes.begin();
    *end++ = "read"sv;

    if (val % file_open_mode::write)
    {
        *end++ = "write"sv;
    }
    if (val % file_open_mode::create)
    {
        *end++ = "create"sv;
    }
    if (val % file_open_mode::truncate)
    {
        *end++ = "truncate"sv;
    }
    fmt::print(str, "(file mode:{})"sv,
               fmt::join(attributes.begin(), end, "|"sv));
    return str;
}
} // namespace vefs

struct vfilesystem_test_dependencies
{
    static constexpr std::array<std::byte, 32> default_user_prk = {};

    llfio::file_handle testFile;
    std::unique_ptr<sector_device> device;
    master_file_info filesystemIndex;

    std::unique_ptr<file_crypto_ctx> cryptoCtx;

    archive_sector_allocator sectorAllocator;

    std::unique_ptr<vfilesystem> testSubject;

    pooled_work_tracker workExecutor;

    vfilesystem_test_dependencies()
        : testFile(vefs::llfio::temp_inode().value())
        , device(sector_device::create_new(testFile.reopen().value(),
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
        testSubject = vfilesystem::create_new(*device, sectorAllocator,
                                              workExecutor, filesystemIndex)
                              .value();
    }
};

BOOST_FIXTURE_TEST_SUITE(vfilesystem_tests, vfilesystem_test_dependencies)

BOOST_AUTO_TEST_CASE(
        recover_sectors_does_not_change_size_if_no_sector_to_recover)
{
    BOOST_TEST(5 == device->size());
    TEST_RESULT_REQUIRE(testSubject->commit());
    TEST_RESULT_REQUIRE(testSubject->recover_unused_sectors());

    BOOST_TEST(5 == device->size());
}

BOOST_AUTO_TEST_CASE(recover_sectors_does_shrinks_size)
{
    auto vfilerx = testSubject->open(
            "testpath", file_open_mode::readwrite | file_open_mode::create);

    TEST_RESULT_REQUIRE(vfilerx);
    auto file = vfilerx.assume_value();
    TEST_RESULT_REQUIRE(file->truncate(0xFFFF));

    TEST_RESULT_REQUIRE(file->commit());
    file = nullptr;

    BOOST_TEST(9 == device->size());
    TEST_RESULT_REQUIRE(testSubject->commit());
    TEST_RESULT_REQUIRE(testSubject->recover_unused_sectors());
    TEST_RESULT_REQUIRE(sectorAllocator.alloc_one());
    TEST_RESULT_REQUIRE(sectorAllocator.alloc_one());
    TEST_RESULT_REQUIRE(sectorAllocator.alloc_one());
    TEST_RESULT_REQUIRE(sectorAllocator.alloc_one());
    TEST_RESULT_REQUIRE(sectorAllocator.alloc_one());
    BOOST_TEST(9 == device->size());
    TEST_RESULT_REQUIRE(sectorAllocator.alloc_one());
    BOOST_TEST(13 == device->size());
}

BOOST_AUTO_TEST_CASE(create_file_allocs_sectors)
{
    auto vfilerx = testSubject->open(
            "testpath", file_open_mode::readwrite | file_open_mode::create);

    TEST_RESULT_REQUIRE(vfilerx);
    auto file = vfilerx.assume_value();
    TEST_RESULT_REQUIRE(file->truncate(0xFFFF));

    TEST_RESULT_REQUIRE(file->commit());
    file = nullptr;

    BOOST_TEST(9 == device->size());
    TEST_RESULT_REQUIRE(testSubject->commit());
    TEST_RESULT_REQUIRE(sectorAllocator.alloc_one());
    TEST_RESULT_REQUIRE(sectorAllocator.alloc_one());
    BOOST_TEST(9 == device->size());
    TEST_RESULT_REQUIRE(sectorAllocator.alloc_one());
    BOOST_TEST(13 == device->size());
}

BOOST_AUTO_TEST_CASE(load_existing_filesystem_keeps_files)
{
    auto vfilerx = testSubject->open(
            "testpath", file_open_mode::readwrite | file_open_mode::create);

    TEST_RESULT_REQUIRE(vfilerx);
    auto file = vfilerx.assume_value();
    TEST_RESULT_REQUIRE(file->truncate(0xFFFF));
    auto writeBlob = utils::make_byte_array(0x9, 0x22, 0x6, 0xde);

    auto write_rx = file->write(writeBlob, 1);

    TEST_RESULT_REQUIRE(file->commit());
    file = nullptr;
    TEST_RESULT_REQUIRE(testSubject->commit());
    filesystemIndex.tree_info = testSubject->committed_root();
    testSubject.reset();
    auto newSectorAllocator = archive_sector_allocator(*device, {});

    auto vfsrx = vfilesystem::open_existing(*device, newSectorAllocator,
                                            workExecutor, filesystemIndex);
    TEST_RESULT_REQUIRE(vfsrx);
    auto existingFileSystem = std::move(vfsrx).assume_value();

    auto reloadedFile
            = existingFileSystem->open("testpath", file_open_mode::read)
                      .value();
    std::array<std::byte, 4> result{};
    TEST_RESULT_REQUIRE(reloadedFile->read(result, 1));

    BOOST_TEST(result == writeBlob);
}

BOOST_AUTO_TEST_CASE(newly_created_file_can_be_found_has_size_zero)
{
    auto file = testSubject
                        ->open("testpath", file_open_mode::readwrite
                                                   | file_open_mode::create)
                        .value();
    (void)file->commit();

    auto result = testSubject->query("testpath").value();

    BOOST_TEST(result.size == 0);
    BOOST_TEST(result.allowed_flags == file_open_mode::readwrite);
}

BOOST_AUTO_TEST_CASE(newly_created_file_is_not_dirty_after_successful_commit)
{
    auto file = testSubject
                        ->open("testpath", file_open_mode::readwrite
                                                   | file_open_mode::create)
                        .value();
    auto commitRx = file->commit();

    auto result = testSubject->query("testpath");

    BOOST_TEST(!commitRx.has_error());
    BOOST_TEST(!file->is_dirty());
}

BOOST_AUTO_TEST_CASE(file_with_size_1000_can_be_found_has_size_1000)
{
    auto vfilerx = testSubject
                           ->open("testpath", file_open_mode::readwrite
                                                      | file_open_mode::create)
                           .value();
    (void)vfilerx->truncate(1000);
    (void)vfilerx->commit();

    auto result = testSubject->query("testpath").value();

    BOOST_TEST(result.size == 1000);
    BOOST_TEST(result.allowed_flags == file_open_mode::readwrite);
}

BOOST_AUTO_TEST_CASE(non_created_file_cannot_be_found)
{
    auto result = testSubject->query("testpath");

    BOOST_TEST(!result);
    BOOST_TEST(result.error() == archive_errc::no_such_file);
}

BOOST_AUTO_TEST_CASE(new_file_system_is_dirty)
{
    auto result = testSubject->query("testpath");

    BOOST_TEST(!result);
    BOOST_TEST(result.error() == archive_errc::no_such_file);
}

BOOST_AUTO_TEST_CASE(filesystem_cannot_commit_non_existing_files)
{
    auto file
            = vefs::vfile::create_new(
                      testSubject.get(), workExecutor, sectorAllocator,
                      file_id(vefs::utils::uuid{
                              0xc7, 0xa5, 0x3d, 0x7a, 0xa4, 0xf0, 0x40, 0x53,
                              0xa7, 0xa3, 0x35, 0xf3, 0x5c, 0xdf, 0x53, 0x3d}),
                      *device, *cryptoCtx)
                      .value();
    auto result = file->commit();

    BOOST_TEST(!result);
    BOOST_TEST(result.error() == archive_errc::no_such_file);
    BOOST_TEST(file->is_dirty());
}

BOOST_AUTO_TEST_CASE(file_in_use_cannot_be_erased)
{
    auto vfilerx = testSubject
                           ->open("testpath", file_open_mode::readwrite
                                                      | file_open_mode::create)
                           .value();

    auto result = testSubject->erase("testpath");

    BOOST_TEST(!result);
    BOOST_TEST(result.error() == errc::still_in_use);
}

BOOST_AUTO_TEST_CASE(file_not_committed_cannot_be_erased_invalid_argument)
{
    auto vfilerx = testSubject
                           ->open("testpath", file_open_mode::readwrite
                                                      | file_open_mode::create)
                           .value();
    vfilerx = nullptr;
    auto result = testSubject->erase("testpath");

    BOOST_TEST(!result);
    //#Todo does this make sense?
    BOOST_TEST(result.error() == errc::invalid_argument);
}

BOOST_AUTO_TEST_CASE(erased_file_cannot_be_queried)
{
    auto vfilerx = testSubject
                           ->open("testpath", file_open_mode::readwrite
                                                      | file_open_mode::create)
                           .value();
    (void)vfilerx->commit();
    vfilerx = nullptr;
    auto result = testSubject->erase("testpath");

    auto queryResult = testSubject->query("testpath");

    BOOST_TEST(!result.has_error());
    BOOST_TEST(!queryResult);
    BOOST_TEST(queryResult.assume_error() == archive_errc::no_such_file);
}

BOOST_AUTO_TEST_CASE(erase_removes_unused_file)
{
    auto vfilerx = testSubject
                           ->open("testpath", file_open_mode::readwrite
                                                      | file_open_mode::create)
                           .value();
    (void)vfilerx->commit();
    vfilerx = nullptr;
    auto result = testSubject->erase("testpath");

    BOOST_TEST(!result.has_error());
}

BOOST_AUTO_TEST_CASE(erasing_non_existing_file_throws_error)
{
    auto result = testSubject->erase("testpath");

    BOOST_TEST(!result);
    BOOST_TEST(result.error() == archive_errc::no_such_file);
}

BOOST_AUTO_TEST_CASE(extract_vfile_to_file)
{
    auto basePath = vefs_tests::current_path.current_path().assume_value();
    std::string_view testFileName = "testfile";

    auto vfilerx = testSubject->open(
            testFileName, file_open_mode::readwrite | file_open_mode::create);

    TEST_RESULT_REQUIRE(vfilerx);
    auto file = vfilerx.assume_value();
    TEST_RESULT_REQUIRE(file->truncate(0xFFFF));
    auto writeBlob = utils::make_byte_array(0x41, 0x42, 0x43, 0x44);

    auto write_rx = file->write(writeBlob, 0);

    TEST_RESULT_REQUIRE(file->commit());
    TEST_RESULT_REQUIRE(testSubject->commit());
    file = nullptr;

    TEST_RESULT_REQUIRE(testSubject->extract({testFileName}, basePath));

    auto basePathHandle = vefs::llfio::path(basePath).assume_value();
    auto fileHandle = vefs::llfio::file(basePathHandle, {testFileName},
                                        vefs::llfio::file_handle::mode::read)
                              .assume_value();

    std::byte array[4];
    auto buffer = vefs::llfio::span<std::byte>({array});

    llfio::byte_io_handle::buffer_type outBuffers[] = {buffer};

    auto result = fileHandle.read({outBuffers, 0}).value();

    BOOST_TEST(result[0] == writeBlob, boost::test_tools::per_element{});
}
BOOST_AUTO_TEST_SUITE_END()
