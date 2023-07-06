
#include "vefs/detail/io_buffer_manager.hpp"
#include "boost-unit-test.hpp"
#include "test-utils.hpp"

namespace vefs_tests
{

BOOST_AUTO_TEST_SUITE(io_buffer_manager_tests)

BOOST_AUTO_TEST_CASE(default_ctor)
{
    vefs::io_buffer_manager subject;
}

BOOST_AUTO_TEST_CASE(factory)
{
    auto rx = vefs::io_buffer_manager::create(1U, 16U);
    TEST_RESULT_REQUIRE(rx);

    [[maybe_unused]] auto const subject = std::move(rx).assume_value();
}

BOOST_AUTO_TEST_CASE(allocate_more_than_buffered)
{
    auto rx = vefs::io_buffer_manager::create(1U, 16U);
    TEST_RESULT_REQUIRE(rx);

    auto const subject = std::move(rx).assume_value();

    std::array<std::span<std::byte>, 17> allocations{};
    vefs::utils::scope_guard freeAllocs = [&]
    {
        for (std::size_t i = 0;
             i < allocations.size() && !allocations[i].empty(); ++i)
        {
            BOOST_TEST_INFO_SCOPE(i);
            subject.deallocate(allocations[i]);
        }
    };

    for (std::size_t i = 0; i < allocations.size(); ++i)
    {
        BOOST_TEST_INFO_SCOPE(i);

        auto allocRx = subject.allocate();
        TEST_RESULT_REQUIRE(allocRx);
        allocations[i] = allocRx.assume_value();
        BOOST_TEST(allocations[i].data() != nullptr);
    }
}

BOOST_AUTO_TEST_CASE(allocations_are_aligned)
{
    auto rx = vefs::io_buffer_manager::create(
            static_cast<std::uint32_t>(vefs::io_buffer_manager::page_size) + 1U,
            16U);
    TEST_RESULT_REQUIRE(rx);

    auto const subject = std::move(rx).assume_value();

    auto tmpAllocRx = subject.allocate();
    TEST_RESULT_REQUIRE(tmpAllocRx);
    vefs::utils::scope_guard freeTmpAlloc = [&]
    {
        subject.deallocate(tmpAllocRx.assume_value());
    };

    auto allocRx = subject.allocate();
    TEST_RESULT_REQUIRE(allocRx);
    vefs::utils::scope_guard freeAlloc = [&]
    {
        subject.deallocate(allocRx.assume_value());
    };

    auto const allocation = allocRx.assume_value().data();

    BOOST_TEST((reinterpret_cast<std::uintptr_t>(allocation)
                & (vefs::io_buffer_manager::page_size - 1U))
               == 0U);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace vefs_tests
