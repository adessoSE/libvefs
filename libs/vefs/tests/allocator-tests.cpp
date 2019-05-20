#include <vefs/allocator/allocation.hpp>
#include <vefs/allocator/multi_pool_mt.hpp>
#include <vefs/allocator/pool_mt.hpp>
#include <vefs/allocator/system.hpp>

#include "boost-unit-test.hpp"
#include "test-utils.hpp"

using namespace vefs::detail;
namespace bdata = boost::unit_test::data;

static constexpr std::size_t size_one = 1;

BOOST_AUTO_TEST_SUITE(allocator_tests)

BOOST_DATA_TEST_CASE(
    system_allocation,
    bdata::random((bdata::engine = test_rng{},
                   bdata::distribution = std::uniform_int_distribution<std::size_t>(0, 1 << 20))) ^
        bdata::xrange(1024),
    size, index)
{
    (void)index;
    using allocator_t = system_allocator<>;
    allocator_t allocator;

    auto maybeAlloc = allocator.allocate(size);
    auto allocationSucceeded = maybeAlloc.has_value();
    BOOST_TEST_REQUIRE(allocationSucceeded);

    allocator.deallocate(maybeAlloc.assume_value());
}

BOOST_AUTO_TEST_CASE(aligned_system_allocation)
{
    using allocator_t = system_allocator<>;

    std::vector<memory_allocation> allocs;

    allocator_t allocator;
    for (auto i = 0; i <= 20; ++i)
    {
        auto maybeAlloc = allocator.allocate(size_one << i);
        auto allocationSucceeded = maybeAlloc.has_value();
        BOOST_TEST(allocationSucceeded);
        if (allocationSucceeded)
        {
            allocs.push_back(maybeAlloc.assume_value());
        }
    }

    for (auto &alloc : allocs)
    {
        allocator.deallocate(alloc);
    }
}

BOOST_AUTO_TEST_CASE(aligned_system_reallocation)
{
    using allocator_t = system_allocator<>;

    std::vector<memory_allocation> allocs;

    allocator_t allocator;
    for (auto i = 0; i <= 20; ++i)
    {
        auto maybeAlloc = allocator.allocate(size_one << i);
        auto allocationSucceeded = maybeAlloc.has_value();
        BOOST_TEST(allocationSucceeded);
        if (allocationSucceeded)
        {
            allocs.push_back(maybeAlloc.assume_value());
        }
    }

    for (auto &alloc : allocs)
    {
        auto n = allocator.reallocate(alloc, alloc.size() << 1);
        auto allocationSucceeded = n.has_value();
        BOOST_TEST(allocationSucceeded);
        if (allocationSucceeded)
        {
            alloc = n.assume_value();
        }
    }

    for (auto &alloc : allocs)
    {
        allocator.deallocate(alloc);
    }
}

BOOST_AUTO_TEST_CASE(overaligned_system_allocation)
{
    using allocator_t = system_allocator<32>;

    std::vector<memory_allocation> allocs;

    allocator_t allocator;
    for (auto i = 0; i <= 20; ++i)
    {
        auto maybeAlloc = allocator.allocate(size_one << i);
        auto allocationSucceeded = maybeAlloc.has_value();
        BOOST_TEST(allocationSucceeded);
        if (allocationSucceeded)
        {
            allocs.push_back(maybeAlloc.assume_value());
        }
    }

    for (auto &alloc : allocs)
    {
        allocator.deallocate(alloc);
    }
}

BOOST_AUTO_TEST_SUITE_END()
