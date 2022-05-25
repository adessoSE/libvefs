#include "vefs/detail/sector_tree_seq.hpp"

#include "boost-unit-test.hpp"
#include "test-utils.hpp"

using namespace vefs;
using namespace vefs::detail;

class allocator_stub
{
public:
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

    auto dealloc_one([[maybe_unused]] sector_id const which) noexcept
            -> result<void>
    {
        return success();
    }
    void dealloc_one([[maybe_unused]] sector_id const which,
                     leak_on_failure_t) noexcept
    {
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

template class vefs::detail::sector_tree_seq<allocator_stub>;

struct sector_tree_seq_pre_create_fixture
{
    using tree_type = sector_tree_seq<allocator_stub>;

    static constexpr std::array<std::byte, 32> default_user_prk{};

    vefs::llfio::file_handle testFile;
    std::unique_ptr<sector_device> device;

    file_crypto_ctx fileCryptoContext;
    root_sector_info rootSectorInfo;

    sector_tree_seq_pre_create_fixture()
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

struct sector_tree_seq_fixture : sector_tree_seq_pre_create_fixture
{
    std::unique_ptr<tree_type> testTree;

    sector_tree_seq_fixture()
        : sector_tree_seq_pre_create_fixture()
        , testTree()
    {
        testTree = tree_type::create_new(*device, fileCryptoContext, *device)
                           .value();

        testTree->commit([this](root_sector_info ri) { rootSectorInfo = ri; })
                .value();
    }

    auto open_test_tree() -> result<std::unique_ptr<tree_type>>
    {
        return tree_type::open_existing(*device, fileCryptoContext,
                                        rootSectorInfo, *device);
    }
};

BOOST_FIXTURE_TEST_SUITE(sector_tree_seq_tests, sector_tree_seq_fixture)

BOOST_FIXTURE_TEST_CASE(create_new, sector_tree_seq_pre_create_fixture)
{
    auto createrx = tree_type::create_new(*device, fileCryptoContext, *device);
    TEST_RESULT_REQUIRE(createrx);
    auto tree = std::move(createrx).assume_value();

    root_sector_info newRootInfo;
    TEST_RESULT_REQUIRE(tree->commit([&newRootInfo](root_sector_info cri)
                                     { newRootInfo = cri; }));

    auto expectedRootMac = vefs::utils::make_byte_array(
            0xe2, 0x1b, 0x52, 0x74, 0xe1, 0xd5, 0x8b, 0x69, 0x87, 0x36, 0x88,
            0x3f, 0x34, 0x4e, 0x5e, 0x2b);

    BOOST_TEST(newRootInfo.root.mac == expectedRootMac);
    BOOST_TEST(newRootInfo.root.sector == sector_id{1});
    BOOST_TEST(newRootInfo.tree_depth == rootSectorInfo.tree_depth);

    BOOST_TEST_REQUIRE(tree->is_loaded());
    auto rootSpan = tree->bytes();
    BOOST_TEST(std::all_of(rootSpan.begin(), rootSpan.end(),
                           [](std::byte v) { return v == std::byte{}; }));
}

BOOST_AUTO_TEST_CASE(open_existing)
{
    testTree.reset();

    auto openrx = tree_type::open_existing(*device, fileCryptoContext,
                                           rootSectorInfo, *device);
    TEST_RESULT_REQUIRE(openrx);
    testTree = std::move(openrx).assume_value();

    BOOST_TEST_REQUIRE(testTree->is_loaded());
    auto rootSpan = testTree->bytes();
    BOOST_TEST(std::all_of(rootSpan.begin(), rootSpan.end(),
                           [](std::byte v) { return v == std::byte{}; }));
}

BOOST_AUTO_TEST_CASE(open_lazy)
{
    TEST_RESULT_REQUIRE(testTree->move_forward(tree_type::access_mode::create));
    TEST_RESULT_REQUIRE(testTree->commit([this](root_sector_info newRoot)
                                         { rootSectorInfo = newRoot; }));
    testTree.reset();

    auto openrx = tree_type::open_lazy(*device, fileCryptoContext,
                                       rootSectorInfo, *device);
    TEST_RESULT_REQUIRE(openrx);
    testTree = std::move(openrx).assume_value();

    BOOST_TEST_REQUIRE(!testTree->is_loaded());

    TEST_RESULT_REQUIRE(testTree->move_to(0U));
    BOOST_TEST_REQUIRE(testTree->is_loaded());

    TEST_RESULT_REQUIRE(testTree->move_forward());
}

BOOST_AUTO_TEST_CASE(expand_to_two_sectors)
{
    TEST_RESULT_REQUIRE(testTree->move_forward(tree_type::access_mode::create));
    testTree->writeable_bytes()[0] = std::byte{0b1010'1010};

    root_sector_info newRootInfo;
    TEST_RESULT_REQUIRE(testTree->commit([&newRootInfo](root_sector_info cri)
                                         { newRootInfo = cri; }));

    auto expectedRootMac = vefs::utils::make_byte_array(
            0xc2, 0xaa, 0x29, 0x03, 0x00, 0x60, 0xb8, 0x4e, 0x3f, 0xc3, 0x57,
            0x2e, 0xed, 0x2d, 0x0d, 0xb5);

    BOOST_TEST(newRootInfo.root.mac == expectedRootMac);
    BOOST_TEST(newRootInfo.root.sector == sector_id{3});
    BOOST_TEST(newRootInfo.tree_depth == 1);
}

BOOST_AUTO_TEST_CASE(shrink_on_commit_if_possible)
{
    TEST_RESULT_REQUIRE(
            testTree->move_to(2019, tree_type::access_mode::create));

    TEST_RESULT_REQUIRE(testTree->commit([this](root_sector_info cri)
                                         { rootSectorInfo = cri; }));

    [[maybe_unused]] auto expectedRootMac = vefs::utils::make_byte_array(
            0xe2, 0x1b, 0x52, 0x74, 0xe1, 0xd5, 0x8b, 0x69, 0x87, 0x36, 0x88,
            0x3f, 0x34, 0x4e, 0x5e, 0x2b);

    // BOOST_TEST(rootSectorInfo.root.mac == expectedRootMac);
    BOOST_TEST(rootSectorInfo.root.sector == sector_id{5});
    BOOST_TEST_REQUIRE(rootSectorInfo.tree_depth == 2);

    testTree.reset();
    auto reopenrx = open_test_tree();
    TEST_RESULT_REQUIRE(reopenrx);
    testTree = std::move(reopenrx).assume_value();

    TEST_RESULT_REQUIRE(testTree->erase_leaf(2019));

    root_sector_info newRootInfo;
    TEST_RESULT_REQUIRE(testTree->commit([&newRootInfo](root_sector_info cri)
                                         { newRootInfo = cri; }));

    // BOOST_TEST(newRootInfo.root.mac == expectedRootMac);
    BOOST_TEST(newRootInfo.root.sector == sector_id{1});
    BOOST_TEST(newRootInfo.tree_depth == 0);
}

BOOST_AUTO_TEST_SUITE_END()
