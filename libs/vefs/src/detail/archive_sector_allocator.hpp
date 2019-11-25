#pragma once

#include <mutex>

#include <vefs/span.hpp>

#include "block_manager.hpp"
#include "sector_device.hpp"

namespace vefs::detail
{
    class archive_sector_allocator
    {
    public:
        enum class leak_on_failure
        {
        };
        static constexpr auto leak_on_failure_v = leak_on_failure{};

        archive_sector_allocator(sector_device &device);

        auto alloc_one() noexcept -> result<sector_id>;
        auto alloc_multiple(span<sector_id> ids) noexcept
            -> result<std::size_t>;

        void dealloc_one(sector_id which) noexcept;

        auto merge_from(utils::block_manager<sector_id> &other) noexcept
            -> result<void>;
        auto merge_disjunct(utils::block_manager<sector_id> &other) noexcept
            -> result<void>;

    private:
        auto mine_new_raw(int num) noexcept -> result<utils::id_range<sector_id>>;
        auto mine_new(int num) noexcept -> result<void>;

        void sectors_leaked() noexcept;

        sector_device &mSectorDevice;
        utils::block_manager<sector_id> mSectorManager;
        std::mutex mAllocatorSync;
    };
} // namespace vefs::detail
