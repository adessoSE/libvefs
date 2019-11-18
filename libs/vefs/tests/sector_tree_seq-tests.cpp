#include "../src/detail/sector_tree_seq.hpp"
                                      
#include "boost-unit-test.hpp"

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

    std::mutex alloc_sync;
    uint64_t alloc_counter;
    sector_device &device;
};

template class vefs::detail::sector_tree_seq<test_allocator>;

BOOST_AUTO_TEST_SUITE(file_walker_tests)



BOOST_AUTO_TEST_SUITE_END()
