#include "boost-unit-test.hpp"
#include "vefs/detail/archive_sector_allocator.hpp"

#include <vefs/detail/preallocated_tree_allocator.hpp>
#include <vefs/detail/sector_tree_seq.hpp>
#include <vefs/utils/binary_codec.hpp>

#include "test-utils.hpp"

using namespace vefs;
using namespace vefs::detail;

namespace
{

struct archive_sector_allocator_dependencies
{
    static constexpr std::array<std::byte, 32> default_user_prk{};

    vefs::llfio::mapped_file_handle testFile;
    file_crypto_ctx fileCryptoContext;
    std::unique_ptr<sector_device> device;

    archive_sector_allocator_dependencies()
        : testFile(vefs::llfio::mapped_temp_inode().value())
        , fileCryptoContext(file_crypto_ctx::zero_init)
        , device(sector_device::create_new(
                         testFile.reopen(0).value(),
                         vefs::test::only_mac_crypto_provider(),
                         default_user_prk)
                         .value()
                         .device)
    {
    }
};

struct archive_sector_allocator_fixture : archive_sector_allocator_dependencies
{
    archive_sector_allocator testSubject;

    archive_sector_allocator_fixture()
        : testSubject(*device, fileCryptoContext.state())
    {
    }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(archive_sector_allocator_tests,
                         archive_sector_allocator_fixture)

BOOST_AUTO_TEST_CASE(alloc_one)
{
    auto resultrx = testSubject.alloc_one();
    TEST_RESULT_REQUIRE(resultrx);
}

BOOST_AUTO_TEST_CASE(dealloc_one)
{
    auto allocrx = testSubject.alloc_one();
    TEST_RESULT_REQUIRE(allocrx);
    auto deallocrx = testSubject.dealloc_one(allocrx.assume_value());
    TEST_RESULT_REQUIRE(deallocrx);
}

BOOST_FIXTURE_TEST_CASE(shrink_large_free_sector_file,
                        archive_sector_allocator_dependencies)
{
    using file_tree_allocator = preallocated_tree_allocator;
    using file_tree = sector_tree_seq<file_tree_allocator>;

    TEST_RESULT_REQUIRE(device->resize(5));
    root_sector_info freeSectorFileRoot{};

    {
        file_tree_allocator::sector_id_container idContainer;
        std::generate_n(
                std::back_inserter(idContainer), 3,
                [n = std::uint64_t{3}]() mutable { return sector_id{n--}; });

        auto &&createTreeRx = file_tree::create_new(*device, fileCryptoContext,
                                                    idContainer);
        TEST_RESULT_REQUIRE(createTreeRx);
        auto &&freeSectorTree = std::move(createTreeRx).assume_value();

        {
            utils::binary_codec<sector_device::sector_payload_size> sector{
                    freeSectorTree->writeable_bytes()};
            sector.write<sector_id>(sector_id{4}, 0);          // start_id
            sector.write<std::uint64_t>(1, sizeof(sector_id)); // num_sectors
        }

        TEST_RESULT_REQUIRE(
                freeSectorTree->move_forward(file_tree::access_mode::force));

        TEST_RESULT_REQUIRE(
                freeSectorTree->commit([&](root_sector_info const &rsi) {
                    freeSectorFileRoot = rsi;
                    freeSectorFileRoot.maximum_extent
                            = detail::sector_device::sector_payload_size * 2U;
                }));
    }

    archive_sector_allocator testSubject(*device, fileCryptoContext.state());
    TEST_RESULT_REQUIRE(testSubject.initialize_from(freeSectorFileRoot));

    auto &&allocrx = testSubject.alloc_one();
    TEST_RESULT_REQUIRE(allocrx);
    BOOST_TEST(allocrx.assume_value() == sector_id{2});
}

BOOST_AUTO_TEST_SUITE_END()
