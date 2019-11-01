#include "../src/detail/sector_tree_mt.hpp"
#include "boost-unit-test.hpp"

#include <vefs/platform/thread_pool.hpp>

#include "../src/detail/basic_archive_file_meta.hpp" // #TODO to be removed
                                         
#include "memfs.hpp"
#include "test-utils.hpp"

using namespace vefs;
using namespace vefs::detail;

class test_allocator
{
public:
    test_allocator(sector_device &device)
        : alloc_sync()
        , alloc_counter(0)
        , device(device)
    {
    }

    auto alloc_one() noexcept -> result<sector_id>
    {
        std::lock_guard allocGuard{alloc_sync};
        sector_id allocated{++alloc_counter};
        VEFS_TRY(device.resize(alloc_counter));
        return allocated;
    }
    auto alloc_multiple(span<sector_id> ids) noexcept -> result<std::size_t>
    {
        return 0;
    }

    auto dealloc_one(const sector_id which) noexcept -> result<void>
    {
        return success();
    }

    auto on_commit() noexcept -> result<void>
    {
        return success();
    }

    std::mutex alloc_sync;
    uint64_t alloc_counter;
    sector_device &device;
};

template class sector_tree_mt<test_allocator, thread_pool>;

struct sector_tree_mt_fixture
{
    using tree_type = sector_tree_mt<test_allocator, thread_pool>;

    static constexpr std::array<std::byte, 32> default_user_prk{};

    filesystem::ptr testFilesystem;
    std::unique_ptr<sector_device> device;

    pooled_work_tracker workExecutor;
    file_crypto_ctx fileCryptoContext;
    root_sector_info rootSectorInfo;

    sector_tree_mt_fixture()
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

BOOST_FIXTURE_TEST_SUITE(sector_tree_mt_tests, sector_tree_mt_fixture)

BOOST_AUTO_TEST_CASE(open_existing)
{
    // #TODO fill with actual data...

    // auto openrx = tree_type::open_existing(*device, fileCryptoContext,
    //                                       workExecutor, rootSectorInfo);
}

BOOST_AUTO_TEST_CASE(create_new)
{
    auto createrx = tree_type::create_new(*device, fileCryptoContext,
                                          workExecutor, *device);

    BOOST_TEST_REQUIRE(createrx);

    auto tree = std::move(createrx).assume_value();
    TEST_RESULT_REQUIRE(tree->commit());

    workExecutor.wait();
}

BOOST_AUTO_TEST_SUITE_END()
