#include "vefs/detail/sector_tree_mt.hpp"
#include "boost-unit-test.hpp"

#include <dplx/cncr/misc.hpp>

#include <vefs/platform/thread_pool.hpp>

#include "vefs/detail/archive_sector_allocator.hpp"
#include "vefs/detail/sector_device.hpp"

#include "test-utils.hpp"

using namespace vefs;
using namespace vefs::detail;

namespace
{

class allocator_stub
{
public:
    std::mutex alloc_sync;
    uint64_t alloc_counter;
    sector_device &device;

    struct sector_allocator
    {
        friend class allocator_stub;

        explicit sector_allocator([[maybe_unused]] allocator_stub &owner,
                                  sector_id current)
            : mCurrent(current)
        {
        }

    private:
        sector_id mCurrent;
    };

    enum class leak_on_failure_t
    {
    };
    static constexpr auto leak_on_failure = leak_on_failure_t{};

    allocator_stub(sector_device &sectorDevice)
        : alloc_sync()
        , alloc_counter(1)
        , device(sectorDevice)
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

    auto dealloc(sector_allocator &) noexcept -> result<void>
    {
        return oc::success();
    }
    void dealloc(sector_allocator &, leak_on_failure_t) noexcept
    {
    }
    auto dealloc_one(sector_id const) noexcept -> result<void>
    {
        return oc::success();
    }
    void dealloc_one(sector_id const, leak_on_failure_t) noexcept
    {
    }

    auto on_commit() noexcept -> result<void>
    {
        return oc::success();
    }

    void on_leak_detected() noexcept
    {
    }
};

} // namespace

template class vefs::detail::sector_tree_mt<allocator_stub>;

struct sector_tree_mt_dependencies
{
    using tree_type = sector_tree_mt<allocator_stub>;
    using read_handle = tree_type::read_handle;
    using write_handle = tree_type::write_handle;

    static constexpr std::array<std::byte, 32> default_user_prk{};

    vefs::llfio::file_handle testFile;
    std::unique_ptr<sector_device> device;

    file_crypto_ctx fileCryptoContext;
    root_sector_info rootSectorInfo;

    sector_tree_mt_dependencies()
        : testFile(vefs::llfio::temp_inode().value())
        , device(sector_device::create_new(
                         testFile.reopen().value(),
                         vefs::test::only_mac_crypto_provider(),
                         default_user_prk)
                         .value()
                         .device)
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
        existingTree
                = tree_type::create_new(*device, fileCryptoContext, *device)
                          .value();

        existingTree
                ->commit([this](root_sector_info ri) { rootSectorInfo = ri; })
                .value();
    }
};

BOOST_FIXTURE_TEST_SUITE(sector_tree_mt_tests, sector_tree_mt_fixture)

BOOST_FIXTURE_TEST_CASE(new_sector_tree_has_id_one, sector_tree_mt_dependencies)
{
    auto createrx = tree_type::create_new(*device, fileCryptoContext, *device);
    TEST_RESULT_REQUIRE(createrx);
    auto newTree = std::move(createrx).assume_value();
    root_sector_info newRootInfo;
    TEST_RESULT_REQUIRE(newTree->commit(
            [&newRootInfo](root_sector_info rsi) { newRootInfo = rsi; }));

    BOOST_TEST(newRootInfo.root.sector == sector_id{2});
    BOOST_TEST(newRootInfo.tree_depth == 0);
}

BOOST_FIXTURE_TEST_CASE(check_initial_sector_tree_mac,
                        sector_tree_mt_dependencies)
{
    auto createrx = tree_type::create_new(*device, fileCryptoContext, *device);
    TEST_RESULT_REQUIRE(createrx);
    auto newTree = std::move(createrx).assume_value();
    root_sector_info newRootInfo;
    TEST_RESULT_REQUIRE(newTree->commit(
            [&newRootInfo](root_sector_info rsi) { newRootInfo = rsi; }));

    auto expectedRootMac = dplx::cncr::make_byte_array<16>(
            {0xe8, 0xfc, 0xa8, 0xd7, 0x5c, 0xd8, 0x51, 0xe1, 0xd6, 0xe3, 0x9e,
             0x2c, 0xec, 0x88, 0x20, 0x69},
            0x00);
    BOOST_TEST(newRootInfo.root.mac == expectedRootMac);
}

BOOST_FIXTURE_TEST_CASE(new_sector_tree_has_node_with_zero_bytes,
                        sector_tree_mt_dependencies)
{
    // given
    auto createrx = tree_type::create_new(*device, fileCryptoContext, *device);
    TEST_RESULT_REQUIRE(createrx);
    auto tree = std::move(createrx).assume_value();
    TEST_RESULT_REQUIRE(tree->commit([](root_sector_info) {}));

    // when
    auto rootAccessRx = tree->access(tree_position{0, 0});
    TEST_RESULT_REQUIRE(rootAccessRx);
    BOOST_TEST_REQUIRE(rootAccessRx.assume_value().operator bool());

    // then
    auto rootSpan = as_span(rootAccessRx.assume_value());
    BOOST_TEST(std::all_of(rootSpan.begin(), rootSpan.end(),
                           [](std::byte v) { return v == std::byte{}; }));
}

BOOST_AUTO_TEST_CASE(
        access_non_existing_node_returns_sector_reference_out_of_range)
{
    // when
    auto rootAccessRx = existingTree->access(tree_position(2));

    // then
    BOOST_TEST(rootAccessRx.has_error());
    BOOST_TEST(rootAccessRx.assume_error()
               == archive_errc::sector_reference_out_of_range);
}

BOOST_AUTO_TEST_CASE(open_existing_tree_creates_existing_tree)
{
    // given
    existingTree.reset();

    // when
    auto openrx = tree_type::open_existing(*device, fileCryptoContext,
                                           rootSectorInfo, *device);
    TEST_RESULT_REQUIRE(openrx);
    auto createdTree = std::move(openrx).assume_value();

    // then
    auto rootAccessRx = createdTree->access(tree_position{0, 0});
    TEST_RESULT_REQUIRE(rootAccessRx);
    auto rootSpan = as_span(rootAccessRx.assume_value());

    BOOST_TEST(std::all_of(rootSpan.begin(), rootSpan.end(),
                           [](std::byte v) { return v == std::byte{}; }));
}

BOOST_AUTO_TEST_CASE(creation_of_a_new_node_changes_mac)
{
    // given
    auto createRx = existingTree->access_or_create(tree_position(1));
    TEST_RESULT_REQUIRE(createRx);
    as_span(createRx.assume_value().as_writable())[0] = std::byte{0b10101010};
    createRx.assume_value() = read_handle();

    root_sector_info newRootInfo;
    TEST_RESULT_REQUIRE(existingTree->commit(
            [&newRootInfo](root_sector_info cri) { newRootInfo = cri; }));

    // then
    auto expectedRootMac = dplx::cncr::make_byte_array<16>(
            {0x64, 0x01, 0xe4, 0xe0, 0x55, 0x19, 0x6d, 0x56, 0x10, 0x17, 0x47,
             0x60, 0xc5, 0x41, 0xfb, 0x19},
            0);
    BOOST_TEST(newRootInfo.root.mac == expectedRootMac);
}

BOOST_AUTO_TEST_CASE(created_node_can_be_read)
{
    // given
    auto const &createdTreePos = tree_position(1);
    auto createRx = existingTree->access_or_create(createdTreePos);
    TEST_RESULT_REQUIRE(createRx);
    write_handle writeHandle = createRx.assume_value().as_writable();
    BOOST_TEST_REQUIRE(writeHandle.operator bool());
    as_span(writeHandle)[0] = std::byte{0b10101010};
    writeHandle = write_handle();

    // when
    TEST_RESULT_REQUIRE(existingTree->commit([](root_sector_info) {}));

    // then
    auto rootAccessRx = existingTree->access(createdTreePos);
    TEST_RESULT_REQUIRE(rootAccessRx);
    auto rootSpan = as_span(rootAccessRx.assume_value());

    BOOST_TEST(rootSpan[0] == std::byte{0b10101010});
}

BOOST_AUTO_TEST_CASE(creation_of_a_new_node_expands_to_two_sectors)
{
    // given
    TEST_RESULT_REQUIRE(existingTree->access_or_create(tree_position(1)));

    // when
    root_sector_info newRootInfo;
    TEST_RESULT_REQUIRE(existingTree->commit(
            [&newRootInfo](root_sector_info rsi) { newRootInfo = rsi; }));

    // then
    BOOST_TEST(newRootInfo.root.sector == sector_id{5});
    BOOST_TEST(newRootInfo.tree_depth == 1);
}

BOOST_AUTO_TEST_CASE(creation_of_a_far_node_expands_to_n_sectors)
{
    // given
    TEST_RESULT_REQUIRE(existingTree->access_or_create(tree_position(1023)));

    // when
    root_sector_info newRootInfo;
    TEST_RESULT_REQUIRE(existingTree->commit(
            [&newRootInfo](root_sector_info rsi) { newRootInfo = rsi; }));

    // then
    BOOST_TEST(newRootInfo.root.sector == sector_id{9});
    BOOST_TEST(newRootInfo.tree_depth == 2);
}

BOOST_AUTO_TEST_CASE(erase_leaf_lets_tree_shrink)
{
    // given
    TEST_RESULT_REQUIRE(existingTree->access_or_create(tree_position(1)));
    TEST_RESULT_REQUIRE(existingTree->commit([](root_sector_info) {}));

    // when
    TEST_RESULT_REQUIRE(existingTree->erase_leaf(1));
    root_sector_info newRootInfo;
    TEST_RESULT_REQUIRE(existingTree->commit(
            [&newRootInfo](root_sector_info rsi) { newRootInfo = rsi; }));

    // then
    BOOST_TEST(newRootInfo.root.sector == sector_id{2});
    BOOST_TEST(newRootInfo.tree_depth == 0);
}

BOOST_AUTO_TEST_CASE(erase_leaf_does_not_let_tree_shrink_if_not_possible)
{
    // given
    TEST_RESULT_REQUIRE(existingTree->access_or_create(tree_position(2)));
    TEST_RESULT_REQUIRE(existingTree->commit([](root_sector_info) {}));

    // when
    TEST_RESULT_REQUIRE(existingTree->erase_leaf(1));
    root_sector_info newRootInfo;
    TEST_RESULT_REQUIRE(existingTree->commit(
            [&newRootInfo](root_sector_info rsi) { newRootInfo = rsi; }));

    // then
    BOOST_TEST(newRootInfo.root.sector == sector_id{5});
    BOOST_TEST(newRootInfo.tree_depth == 1);
}

BOOST_AUTO_TEST_CASE(erase_leaf_for_position_0_is_not_supported)
{
    // when
    auto eraseResult = existingTree->erase_leaf(0);

    // then
    BOOST_TEST(eraseResult.has_error());
    BOOST_TEST(eraseResult.assume_error() == errc::not_supported);
}

BOOST_AUTO_TEST_CASE(erase_leaf_for_not_existing_leaf_does_not_do_anything)
{
    // when
    auto eraseResult = existingTree->erase_leaf(1);

    // then
    root_sector_info newRootInfo;
    TEST_RESULT_REQUIRE(existingTree->commit(
            [&newRootInfo](root_sector_info cri) { newRootInfo = cri; }));
    auto expectedRootMac = dplx::cncr::make_byte_array<16>(
            {0xe8, 0xfc, 0xa8, 0xd7, 0x5c, 0xd8, 0x51, 0xe1, 0xd6, 0xe3, 0x9e,
             0x2c, 0xec, 0x88, 0x20, 0x69},
            0x00);

    BOOST_TEST(newRootInfo.root.mac == expectedRootMac);
    BOOST_TEST(newRootInfo.tree_depth == 0);
}

BOOST_AUTO_TEST_CASE(erase_leaf_changes_mac)
{
    // given
    TEST_RESULT_REQUIRE(existingTree->access_or_create(
            tree_position(0x00ff'ffff'ffff'ffffU)));
    root_sector_info preRootInfo;
    TEST_RESULT_REQUIRE(existingTree->commit(
            [&preRootInfo](root_sector_info cri) { preRootInfo = cri; }));

    // when
    TEST_RESULT_REQUIRE(existingTree->erase_leaf(0x00ff'ffff'ffff'ffffU));
    root_sector_info newRootInfo;
    TEST_RESULT_REQUIRE(existingTree->commit(
            [&newRootInfo](root_sector_info cri) { newRootInfo = cri; }));

    // then
    BOOST_TEST(newRootInfo.root.mac != preRootInfo.root.mac);
    BOOST_TEST(newRootInfo.tree_depth != preRootInfo.tree_depth);
}

BOOST_AUTO_TEST_CASE(increasing_tree_height_doesnt_deadlock)
{
    // fill cache with dirty pages
    for (unsigned i = 0; i < 1024; ++i)
    {
        (void)existingTree->access_or_create(tree_position{i})
                .value()
                .as_writable();
    }

    // force height increase
    TEST_RESULT_REQUIRE(existingTree->access_or_create(
            tree_position{vefs::detail::lut::ref_width[2]}));

    TEST_RESULT_REQUIRE(existingTree->commit([](root_sector_info) {}));
}

BOOST_AUTO_TEST_SUITE_END()
