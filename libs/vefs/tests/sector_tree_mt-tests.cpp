#include "../src/detail/sector_tree_mt.hpp"
#include "boost-unit-test.hpp"

#include <vefs/platform/thread_pool.hpp>

#include "../src/detail/archive_sector_allocator.hpp"

#include "test-utils.hpp"

using namespace vefs;
using namespace vefs::detail;

class allocator_stub
{
public:
    struct sector_allocator
    {
        friend class allocator_stub;

        explicit sector_allocator(allocator_stub &owner, sector_id current)
            : mCurrent(current)
        {
        }

    private:
        sector_id mCurrent;
    };

    allocator_stub(sector_device &device)
        : alloc_sync()
        , alloc_counter(1)
        , device(device)
    {
    }

    auto reallocate(sector_allocator &forWhich) noexcept -> result<sector_id>
    {
        if (forWhich.mCurrent != sector_id{})
        {
            return forWhich.mCurrent;
        }
        std::lock_guard allocGuard{alloc_sync};
        sector_id allocated{alloc_counter++};
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

template class vefs::detail::sector_tree_mt<allocator_stub, thread_pool>;

struct sector_tree_mt_dependencies
{
    using tree_type = sector_tree_mt<allocator_stub, thread_pool>;
    using read_handle = tree_type::read_handle;
    using write_handle = tree_type::write_handle;

    static constexpr std::array<std::byte, 32> default_user_prk{};

    vefs::llfio::mapped_file_handle testFile;
    std::unique_ptr<sector_device> device;

    pooled_work_tracker workExecutor;
    file_crypto_ctx fileCryptoContext;
    root_sector_info rootSectorInfo;

    sector_tree_mt_dependencies()
        : testFile(vefs::llfio::mapped_temp_inode().value())
        , device(sector_device::open(testFile.clone(0).value(),
                                     crypto::debug_crypto_provider(),
                                     default_user_prk, true)
                     .value())
        , workExecutor(&thread_pool::shared()) // #TODO create test thread pool
        , fileCryptoContext(file_crypto_ctx::zero_init)
        , rootSectorInfo()
    {
    }
};

struct sector_tree_mt_fixture : sector_tree_mt_dependencies
{
    std::unique_ptr<tree_type> existingTree;

    sector_tree_mt_fixture()
        : sector_tree_mt_dependencies()
        , existingTree()
    {
        existingTree = tree_type::create_new(*device, fileCryptoContext,
                                             workExecutor, *device)
                       .value();

        rootSectorInfo = existingTree->commit().value();
    }
};

BOOST_FIXTURE_TEST_SUITE(sector_tree_mt_tests, sector_tree_mt_fixture)

BOOST_FIXTURE_TEST_CASE(new_sector_tree_has_id_one, sector_tree_mt_dependencies)
{
    auto createrx = tree_type::create_new(*device, fileCryptoContext,
                                          workExecutor, *device);
    TEST_RESULT_REQUIRE(createrx);
    auto new_tree = std::move(createrx).assume_value();
    auto commitRx = new_tree->commit();
    TEST_RESULT_REQUIRE(commitRx);

    auto &&newRootInfo = commitRx.assume_value();
    BOOST_TEST(newRootInfo.root.sector == sector_id{1});
    BOOST_TEST(newRootInfo.tree_depth == 0);
}

BOOST_FIXTURE_TEST_CASE(check_initial_sector_tree_mac, sector_tree_mt_dependencies)
{
    auto createrx = tree_type::create_new(*device, fileCryptoContext,
                                          workExecutor, *device);
    TEST_RESULT_REQUIRE(createrx);
    auto new_tree = std::move(createrx).assume_value();
    auto commitRx = new_tree->commit();
    TEST_RESULT_REQUIRE(commitRx);

    auto expectedRootMac = vefs::utils::make_byte_array(
        0xe2, 0x1b, 0x52, 0x74, 0xe1, 0xd5, 0x8b, 0x69, 0x87, 0x36, 0x88, 0x3f,
        0x34, 0x4e, 0x5e, 0x2b);
    auto &&newRootInfo = commitRx.assume_value();
    BOOST_TEST(newRootInfo.root.mac == expectedRootMac);
}

BOOST_FIXTURE_TEST_CASE(new_sector_tree_has_node_with_zero_bytes, sector_tree_mt_dependencies)
{
    //given
    auto createrx = tree_type::create_new(*device, fileCryptoContext,
                                          workExecutor, *device);
    TEST_RESULT_REQUIRE(createrx);
    auto tree = std::move(createrx).assume_value();
    auto commitRx = tree->commit();
    TEST_RESULT_REQUIRE(commitRx);

    //when
    auto rootAccessRx = tree->access(tree_position{0, 0});
    TEST_RESULT_REQUIRE(rootAccessRx);

    //then
    auto rootSpan = as_span(rootAccessRx.assume_value());
    BOOST_TEST(std::all_of(rootSpan.begin(), rootSpan.end(),
                           [](std::byte v) { return v == std::byte{}; }));
}

BOOST_AUTO_TEST_CASE(access_non_existing_node_returns_sector_reference_out_of_range)
{
    //when
    auto rootAccessRx = existingTree->access(tree_position(2));

    //then
    BOOST_TEST(rootAccessRx.has_error());
    BOOST_TEST(rootAccessRx.assume_error() == archive_errc::sector_reference_out_of_range);
}

BOOST_AUTO_TEST_CASE(open_existing_tree_creates_existing_tree)
    {
        //given
        existingTree.reset();

        //when
        auto openrx = tree_type::open_existing(
                *device, fileCryptoContext, workExecutor, rootSectorInfo, *device);
        TEST_RESULT_REQUIRE(openrx);
        auto createdTree = std::move(openrx).assume_value();

        //then
        auto rootAccessRx = createdTree->access(tree_position{0, 0});
        TEST_RESULT_REQUIRE(rootAccessRx);
        auto rootSpan = as_span(rootAccessRx.assume_value());

        BOOST_TEST(std::all_of(rootSpan.begin(), rootSpan.end(),
                               [](std::byte v) { return v == std::byte{}; }));
    }

BOOST_AUTO_TEST_CASE(creation_of_a_new_node_changes_mac)
{
    //given
    auto createRx = existingTree->access_or_create(tree_position(1));
    TEST_RESULT_REQUIRE(createRx);
    as_span(write_handle(createRx.assume_value()))[0] = std::byte{0b1010'1010};
    createRx.assume_value() = read_handle();

    //when
    auto commitRx = existingTree->commit();
    TEST_RESULT_REQUIRE(commitRx);
    auto &&newRootInfo = std::move(commitRx).assume_value();

    //then
    auto expectedRootMac = vefs::utils::make_byte_array(
        0xc2, 0xaa, 0x29, 0x03, 0x00, 0x60, 0xb8, 0x4e, 0x3f, 0xc3, 0x57, 0x2e,
        0xed, 0x2d, 0x0d, 0xb5);
    BOOST_TEST(newRootInfo.root.mac == expectedRootMac);
}

BOOST_AUTO_TEST_CASE(created_node_can_be_read)
{
    //given
    auto const &createdTreePos = tree_position(1);
    auto createRx = existingTree->access_or_create(createdTreePos);
    TEST_RESULT_REQUIRE(createRx);
    as_span(write_handle(createRx.assume_value()))[0] = std::byte{0b1010'1010};
    createRx.assume_value() = read_handle();

    //when
    auto commitRx = existingTree->commit();
    TEST_RESULT_REQUIRE(commitRx);
    auto &&newRootInfo = std::move(commitRx).assume_value();

    //then
    auto rootAccessRx = existingTree->access(createdTreePos);
    TEST_RESULT_REQUIRE(rootAccessRx);
    auto rootSpan = as_span(rootAccessRx.assume_value());

    BOOST_TEST(rootSpan[0] == std::byte{0b1010'1010});
}

BOOST_AUTO_TEST_CASE(creation_of_a_new_node_expands_to_two_sectors)
{
    //given
    auto createRx = existingTree->access_or_create(tree_position(1));
    TEST_RESULT_REQUIRE(createRx);

    //when
    auto commitRx = existingTree->commit();
    TEST_RESULT_REQUIRE(commitRx);
    auto &&newRootInfo = std::move(commitRx).assume_value();

    //then
    BOOST_TEST(newRootInfo.root.sector == sector_id{3});
    BOOST_TEST(newRootInfo.tree_depth == 1);
}

BOOST_AUTO_TEST_CASE(erase_leaf_lets_tree_shrink)
{
    //given
    TEST_RESULT_REQUIRE(existingTree->access_or_create(tree_position(1)));
    auto commitRx = existingTree->commit();
    TEST_RESULT_REQUIRE(commitRx);

    //when
    TEST_RESULT_REQUIRE(existingTree->erase_leaf(1));
    auto eraseCommitRx = existingTree->commit();
    TEST_RESULT_REQUIRE(eraseCommitRx);

    //then
    auto &&newRootInfo = std::move(eraseCommitRx).assume_value();
    BOOST_TEST(newRootInfo.root.sector == sector_id{1});
    BOOST_TEST(newRootInfo.tree_depth == 0);
}

BOOST_AUTO_TEST_CASE(erase_leaf_does_not_let_tree_shrink_if_not_possible)
{
    //given
    TEST_RESULT_REQUIRE(existingTree->access_or_create(tree_position(2)));
    auto commitRx = existingTree->commit();
    TEST_RESULT_REQUIRE(commitRx);

    //when
    TEST_RESULT_REQUIRE(existingTree->erase_leaf(1));
    auto eraseCommitRx = existingTree->commit();
    TEST_RESULT_REQUIRE(eraseCommitRx);

    //then
    auto &&newRootInfo = std::move(eraseCommitRx).assume_value();
    BOOST_TEST(newRootInfo.root.sector == sector_id{3});
    BOOST_TEST(newRootInfo.tree_depth == 1);
}

BOOST_AUTO_TEST_CASE(erase_leaf_for_position_0_is_not_supported)
{
    //when
    auto eraseResult = existingTree->erase_leaf(0);

    //then
    BOOST_TEST(eraseResult.has_error());
    BOOST_TEST(eraseResult.assume_error() == errc::not_supported);
}

BOOST_AUTO_TEST_CASE(erase_leaf_for_not_existing_leaf_does_not_do_anything)
{
    //when
    auto eraseResult = existingTree->erase_leaf(1);

    //then
    auto commitRx = existingTree->commit();
    auto &&newRootInfo = std::move(commitRx).assume_value();
    auto expectedRootMac = vefs::utils::make_byte_array(
            0xe2, 0x1b, 0x52, 0x74, 0xe1, 0xd5, 0x8b, 0x69, 0x87, 0x36, 0x88, 0x3f,
            0x34, 0x4e, 0x5e, 0x2b);

    BOOST_TEST(newRootInfo.root.mac == expectedRootMac);
    BOOST_TEST(newRootInfo.tree_depth == 0);
}

BOOST_AUTO_TEST_CASE(erase_leaf_changes_mac)
{
    //given
    TEST_RESULT_REQUIRE(existingTree->access_or_create(tree_position(1)));
    auto commitRx = existingTree->commit();
    TEST_RESULT_REQUIRE(commitRx);

    //when
    TEST_RESULT_REQUIRE(existingTree->erase_leaf(1));
    commitRx = existingTree->commit();
    TEST_RESULT_REQUIRE(commitRx);

    //then
    auto &&newRootInfo = std::move(commitRx).assume_value();
    auto expectedRootMac = vefs::utils::make_byte_array(
            0xe2, 0x1b, 0x52, 0x74, 0xe1, 0xd5, 0x8b, 0x69, 0x87, 0x36, 0x88, 0x3f,
            0x34, 0x4e, 0x5e, 0x2b);
    BOOST_TEST(newRootInfo.root.mac == expectedRootMac);
}

BOOST_AUTO_TEST_SUITE_END()
