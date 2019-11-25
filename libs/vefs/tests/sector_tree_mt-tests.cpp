#include "../src/detail/sector_tree_mt.hpp"
#include "boost-unit-test.hpp"

#include <vefs/platform/thread_pool.hpp>

#include "../src/detail/basic_archive_file_meta.hpp" // #TODO to be removed
#include "../src/detail/archive_sector_allocator.hpp"

#include "memfs.hpp"
#include "test-utils.hpp"

using namespace vefs;
using namespace vefs::detail;

class test_allocator
{
public:
    struct sector_allocator
    {
        friend class test_allocator;

        explicit sector_allocator(test_allocator &owner, sector_id current)
            : mCurrent(current)
        {
        }

    private:
        sector_id mCurrent;
    };

    test_allocator(sector_device &device)
        : alloc_sync()
        , alloc_counter(0)
        , device(device)
    {
    }

    auto alloc_one_() noexcept -> result<sector_id>
    {
        std::lock_guard allocGuard{alloc_sync};
        sector_id allocated{++alloc_counter};
        VEFS_TRY(device.resize(alloc_counter));
        return allocated;
    }
    auto alloc_multiple_(span<sector_id> ids) noexcept -> result<std::size_t>
    {
        std::lock_guard allocGuard{alloc_sync};
        auto newSize = alloc_counter + ids.size();
        VEFS_TRY(device.resize(newSize));
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            ids[i] = sector_id{alloc_counter + i};
        }
        alloc_counter = newSize;
        return ids.size();
    }

    auto reallocate(sector_allocator &forWhich) noexcept -> result<sector_id>
    {
        if (forWhich.mCurrent != sector_id{})
        {
            return forWhich.mCurrent;
        }
        std::lock_guard allocGuard{alloc_sync};
        sector_id allocated{++alloc_counter};
        VEFS_TRY(device.resize(alloc_counter));
        return allocated;
    }

    auto dealloc_one(const sector_id which) noexcept -> result<void>
    {
        return success();
    }

    auto on_commit() noexcept -> result<void>
    {
        return success();
    }

    void on_leak_detected() noexcept
    {
    }

    std::mutex alloc_sync;
    uint64_t alloc_counter;
    sector_device &device;
};

template class vefs::detail::sector_tree_mt<test_allocator, thread_pool>;

struct sector_tree_mt_pre_create_fixture
{
    using tree_type = sector_tree_mt<test_allocator, thread_pool>;
    using read_handle = tree_type::read_handle;
    using write_handle = tree_type::write_handle;

    static constexpr std::array<std::byte, 32> default_user_prk{};

    filesystem::ptr testFilesystem;
    std::unique_ptr<sector_device> device;

    pooled_work_tracker workExecutor;
    file_crypto_ctx fileCryptoContext;
    root_sector_info rootSectorInfo;

    sector_tree_mt_pre_create_fixture()
        : testFilesystem(tests::memory_filesystem::create())
        , device(sector_device::open(
                     testFilesystem, "tree-test.vefs",
                     crypto::debug_crypto_provider(), default_user_prk,
                     file_open_mode::readwrite | file_open_mode::create)
                     .value())
        , workExecutor(&thread_pool::shared()) // #TODO create test thread pool
        , fileCryptoContext()
        , rootSectorInfo()
    {
    }
};

struct sector_tree_mt_fixture : sector_tree_mt_pre_create_fixture
{
    std::unique_ptr<tree_type> testTree;

    sector_tree_mt_fixture()
        : sector_tree_mt_pre_create_fixture()
        , testTree()
    {
        testTree = tree_type::create_new(*device, fileCryptoContext,
                                         workExecutor, *device)
                       .value();

        rootSectorInfo = testTree->commit().value();
    }

    auto open_test_tree() -> result<std::unique_ptr<tree_type>>
    {
        return tree_type::open_existing(*device, fileCryptoContext,
                                        workExecutor, rootSectorInfo, *device);
    }
};

BOOST_FIXTURE_TEST_SUITE(sector_tree_mt_tests, sector_tree_mt_fixture)

BOOST_FIXTURE_TEST_CASE(create_new, sector_tree_mt_pre_create_fixture)
{
    auto createrx = tree_type::create_new(*device, fileCryptoContext,
                                          workExecutor, *device);
    TEST_RESULT_REQUIRE(createrx);
    auto tree = std::move(createrx).assume_value();

    auto commitRx = tree->commit();
    TEST_RESULT_REQUIRE(commitRx);
    auto &&newRootInfo = std::move(commitRx).assume_value();

    auto expectedRootMac = vefs::utils::make_byte_array(
        0xe2, 0x1b, 0x52, 0x74, 0xe1, 0xd5, 0x8b, 0x69, 0x87, 0x36, 0x88, 0x3f,
        0x34, 0x4e, 0x5e, 0x2b);

    BOOST_TEST(newRootInfo.root.mac == expectedRootMac);
    BOOST_TEST(newRootInfo.root.sector == sector_id{1});
    BOOST_TEST(newRootInfo.tree_depth == rootSectorInfo.tree_depth);

    auto rootAccessRx = tree->access(tree_position{0, 0});
    TEST_RESULT_REQUIRE(rootAccessRx);
    auto rootSpan = as_span(rootAccessRx.assume_value());
    BOOST_TEST(std::all_of(rootSpan.begin(), rootSpan.end(),
                           [](std::byte v) { return v == std::byte{}; }));
}

BOOST_AUTO_TEST_CASE(open_existing)
{
    testTree.reset();

    auto openrx = tree_type::open_existing(
        *device, fileCryptoContext, workExecutor, rootSectorInfo, *device);
    TEST_RESULT_REQUIRE(openrx);
    testTree = std::move(openrx).assume_value();

    auto rootAccessRx = testTree->access(tree_position{0, 0});
    TEST_RESULT_REQUIRE(rootAccessRx);
    auto rootSpan = as_span(rootAccessRx.assume_value());
    BOOST_TEST(std::all_of(rootSpan.begin(), rootSpan.end(),
                           [](std::byte v) { return v == std::byte{}; }));
}

BOOST_AUTO_TEST_CASE(expand_to_two_sectors)
{
    auto createRx = testTree->access_or_create(tree_position(1));
    TEST_RESULT_REQUIRE(createRx);
    as_span(write_handle(createRx.assume_value()))[0] = std::byte{0b1010'1010};
    createRx.assume_value() = read_handle();

    auto commitRx = testTree->commit();
    TEST_RESULT_REQUIRE(commitRx);
    auto &&newRootInfo = std::move(commitRx).assume_value();

    auto expectedRootMac = vefs::utils::make_byte_array(
        0xc2, 0xaa, 0x29, 0x03, 0x00, 0x60, 0xb8, 0x4e, 0x3f, 0xc3, 0x57, 0x2e,
        0xed, 0x2d, 0x0d, 0xb5);

    BOOST_TEST(newRootInfo.root.mac == expectedRootMac);
    BOOST_TEST(newRootInfo.root.sector == sector_id{3});
    BOOST_TEST(newRootInfo.tree_depth == 1);
}

BOOST_AUTO_TEST_CASE(shrink_on_commit_if_possible)
{
    TEST_RESULT_REQUIRE(testTree->access_or_create(tree_position(1)));

    auto commitRx = testTree->commit();
    TEST_RESULT_REQUIRE(commitRx);
    rootSectorInfo = std::move(commitRx).assume_value();

    auto expectedRootMac = vefs::utils::make_byte_array(
        0xe2, 0x1b, 0x52, 0x74, 0xe1, 0xd5, 0x8b, 0x69, 0x87, 0x36, 0x88, 0x3f,
        0x34, 0x4e, 0x5e, 0x2b);

    // BOOST_TEST(rootSectorInfo.root.mac == expectedRootMac);
    BOOST_TEST(rootSectorInfo.root.sector == sector_id{3});
    BOOST_TEST_REQUIRE(rootSectorInfo.tree_depth == 1);

    testTree.reset();
    auto reopenrx = open_test_tree();
    TEST_RESULT_REQUIRE(reopenrx);
    testTree = std::move(reopenrx).assume_value();

    TEST_RESULT_REQUIRE(testTree->erase_leaf(1));

    commitRx = testTree->commit();
    TEST_RESULT_REQUIRE(commitRx);
    auto &&newRootInfo = std::move(commitRx).assume_value();

    BOOST_TEST(newRootInfo.root.mac == expectedRootMac);
    BOOST_TEST(newRootInfo.root.sector == sector_id{1});
    BOOST_TEST(newRootInfo.tree_depth == 0);
}

BOOST_AUTO_TEST_SUITE_END()
