#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <array>
#include <atomic>
#include <limits>
#include <thread>
#include <tuple>
#include <type_traits>

#include <vefs/allocator/alignment.hpp>
#include <vefs/allocator/allocation.hpp>
#include <vefs/allocator/atomic_resource_counter.hpp>
#include <vefs/allocator/pool_alloc_map_mt.hpp>
#include <vefs/exceptions.hpp>
#include <vefs/utils/bit_scan.hpp>
#include <vefs/utils/misc.hpp>

namespace vefs::detail
{
    template <std::size_t ELEM_SIZE, std::size_t NUM_ELEMS, typename BlockAllocator,
              std::size_t ALIGNMENT = BlockAllocator::alignment>
    class pool_allocator_mt : private BlockAllocator
    {
        using block_allocator_t = BlockAllocator;
        using cblock_unit_t = std::atomic<std::size_t>;
        using avals = alignmnent_values<ELEM_SIZE, ALIGNMENT>;

    public:
        static constexpr bool is_thread_safe = true;
        static constexpr std::size_t alignment = ALIGNMENT;
        static_assert(block_allocator_t::alignment % alignment == 0,
                      "the underlying block allocator must provide an already aligned block");

    private:
        static constexpr std::size_t num_elems = NUM_ELEMS;
        static constexpr std::size_t alloc_block_size = num_elems * avals::adj_elem_size;

        using alloc_ctr_value_type = fast_atomic_uint_for_maxval<num_elems>;
        using alloc_ctr_atomic_type = atomic_resource_counter<alloc_ctr_value_type, num_elems>;

        constexpr static auto prealloc(block_allocator_t &src) -> rw_blob<alloc_block_size>;

    public:
        constexpr pool_allocator_mt();
        inline ~pool_allocator_mt();

        inline auto intr_allocate(const std::size_t size) noexcept
            -> result<std::tuple<memory_allocation, std::size_t>>;

        inline auto allocate(const std::size_t size) noexcept -> allocation_result;

        inline auto reallocate(const memory_allocation memblock, const std::size_t size) noexcept
            -> allocation_result;

        inline void deallocate(memory_allocation memblock) noexcept;

        inline bool owns(const memory_allocation memblock) const noexcept;

    private:
        rw_blob<alloc_block_size> mBlock;

        alloc_ctr_atomic_type mAllocCtr;
        pool_alloc_map_mt<num_elems> mAllocMap;
    };

    template <std::size_t ELEM_SIZE, std::size_t NUM_ELEMS, typename BlockAllocator,
              std::size_t ALIGNMENT>
    constexpr pool_allocator_mt<ELEM_SIZE, NUM_ELEMS, BlockAllocator, ALIGNMENT>::pool_allocator_mt()
        : block_allocator_t()
        , mBlock{prealloc(*this)}
        , mAllocCtr{resource_is_initialized}
        , mAllocMap{}
    {
    }
    template <std::size_t ELEM_SIZE, std::size_t NUM_ELEMS, typename BlockAllocator,
              std::size_t ALIGNMENT>
    inline pool_allocator_mt<ELEM_SIZE, NUM_ELEMS, BlockAllocator, ALIGNMENT>::~pool_allocator_mt()
    {
        block_allocator_t::deallocate({mBlock.data(), mBlock.size()});
    }

    template <std::size_t ELEM_SIZE, std::size_t NUM_ELEMS, typename BlockAllocator,
              std::size_t ALIGNMENT>
    constexpr auto pool_allocator_mt<ELEM_SIZE, NUM_ELEMS, BlockAllocator, ALIGNMENT>::prealloc(
        block_allocator_t &src) -> rw_blob<alloc_block_size>
    {
        if (allocation_result block = src.allocate(alloc_block_size);
            block.has_value())
        {
            return block.assume_value().writeable_bytes().first<alloc_block_size>();
        }
        else if (block.assume_error() == errc::not_enough_memory ||
                 block.assume_error() == std::errc::not_enough_memory)
        {
            BOOST_THROW_EXCEPTION(std::bad_alloc{});
        }
        else
        {
            throw error_exception{std::move(block).assume_error()};
        }
    }

    template <std::size_t ELEM_SIZE, std::size_t NUM_ELEMS, typename BlockAllocator,
              std::size_t ALIGNMENT>
    inline auto pool_allocator_mt<ELEM_SIZE, NUM_ELEMS, BlockAllocator, ALIGNMENT>::intr_allocate(
        const std::size_t size) noexcept
        -> result<std::tuple<memory_allocation, std::size_t>>
    {
        if (size > avals::elem_size)
        {
            return errc::not_supported;
        }

        if (mAllocCtr.try_aquire_one() != resource_acquire_result::success)
        {
            return errc::not_enough_memory;
        }

        auto blockPos = mAllocMap.reserve_slot();
        auto memory = mBlock.subspan(blockPos * avals::adj_elem_size, size);
        return std::tuple{memory_allocation{memory.data(), memory.size()}, blockPos};
    }

    template <std::size_t ELEM_SIZE, std::size_t NUM_ELEMS, typename BlockAllocator,
              std::size_t ALIGNMENT>
    inline auto pool_allocator_mt<ELEM_SIZE, NUM_ELEMS, BlockAllocator, ALIGNMENT>::allocate(
        const std::size_t size) noexcept -> allocation_result
    {
        BOOST_OUTCOME_TRY(alloc, intr_allocate(size));
        return std::get<memory_allocation>(alloc);
    }

    template <std::size_t ELEM_SIZE, std::size_t NUM_ELEMS, typename BlockAllocator,
              std::size_t ALIGNMENT>
    inline auto pool_allocator_mt<ELEM_SIZE, NUM_ELEMS, BlockAllocator, ALIGNMENT>::reallocate(
        const memory_allocation memblock, const std::size_t size) noexcept
        -> allocation_result
    {
        assert(owns(memblock));

        if (size > avals::elem_size)
        {
            return errc::not_supported;
        }
        const auto memspan = memblock.writeable_bytes();
        return memory_allocation{memspan.data(), size};
    }

    template <std::size_t ELEM_SIZE, std::size_t NUM_ELEMS, typename BlockAllocator,
              std::size_t ALIGNMENT>
    inline void pool_allocator_mt<ELEM_SIZE, NUM_ELEMS, BlockAllocator, ALIGNMENT>::deallocate(
        memory_allocation memblock) noexcept
    {
        assert(owns(memblock));

        auto *const ptr = memblock.writeable_bytes().data();
        const auto idx = (ptr - mBlock.data()) / avals::adj_elem_size;

        mAllocMap.release_slot(idx);

        [[maybe_unused]] auto action = mAllocCtr.release_one(false);
    }

    template <std::size_t ELEM_SIZE, std::size_t NUM_ELEMS, typename BlockAllocator,
              std::size_t ALIGNMENT>
    inline bool pool_allocator_mt<ELEM_SIZE, NUM_ELEMS, BlockAllocator, ALIGNMENT>::owns(
        const memory_allocation memblock) const noexcept
    {
        const auto memspan = memblock.writeable_bytes();

        std::byte *start = memspan.data();
        std::byte *end = memspan.data() + memspan.size();
        std::byte *my_end = mBlock.data() + alloc_block_size;
        return mBlock.data() <= start && end <= my_end &&
               ((start - mBlock.data()) % avals::adj_elem_size) == 0 &&
               memspan.size() <= avals::elem_size;
    }

} // namespace vefs::detail
