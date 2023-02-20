#include "boost-unit-test.hpp"
#include "vefs/detail/archive_sector_allocator.hpp"

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
        , device(sector_device::create_new(
                         testFile.reopen(0).value(),
                         vefs::test::only_mac_crypto_provider(),
                         default_user_prk)
                         .value()
                         .device)
        , fileCryptoContext(file_crypto_ctx::zero_init)
    {
    }
};

struct archive_sector_allocator_fixture : archive_sector_allocator_dependencies
{
    static constexpr std::array<std::byte, 32> default_user_prk{};

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

BOOST_AUTO_TEST_CASE(dealloc_one_leak_on_failure)
{
    auto allocrx = testSubject.alloc_one();
    TEST_RESULT_REQUIRE(allocrx);
    testSubject.dealloc_one(allocrx.assume_value(),
                            archive_sector_allocator::leak_on_failure);
    BOOST_TEST(testSubject.sector_leak_detected());
}

BOOST_AUTO_TEST_SUITE_END()
