#pragma once

#include <cassert>
#include <cstddef>

#include <array>
#include <atomic>
#include <type_traits>
#include <utility>

#include <boost/predef.h>

#include <vefs/allocator/alignment.hpp>
#include <vefs/allocator/allocation.hpp>
#include <vefs/allocator/atomic_resource_counter.hpp>
#include <vefs/allocator/pool_alloc_map_mt.hpp>
#include <vefs/utils/misc.hpp>

namespace vefs::detail
{
template <std::size_t ELEM_SIZE,
          std::size_t NUM_ELEMS_PER_BLOCK,
          std::size_t NUM_BLOCKS,
          typename BlockAllocator,
          std::size_t ALIGNMENT = BlockAllocator::alignment>
class multi_pool_allocator_mt : private BlockAllocator
{
    using block_allocator_t = BlockAllocator;
    // using cblock_unit_t = std::atomic<std::size_t>;
    using avals = alignmnent_values<ELEM_SIZE, ALIGNMENT>;

public:
    static constexpr bool is_thread_safe = true;
    static constexpr std::size_t alignment = ALIGNMENT;
    static_assert(block_allocator_t::alignment % alignment == 0,
                  "the underlying block allocator must provide an already "
                  "aligned block");

private:
    static constexpr std::size_t num_elems_per_block = NUM_ELEMS_PER_BLOCK;
    static constexpr std::size_t num_blocks = NUM_BLOCKS;
    static constexpr std::size_t max_elems = num_elems_per_block * num_blocks;

    static constexpr std::size_t alloc_block_size
            = num_elems_per_block * avals::adj_elem_size;

    using galloc_ctr_value_type
            = detail::fast_atomic_uint_for_maxval<max_elems>;
    using galloc_ctr_atomic_type
            = detail::atomic_resource_counter<galloc_ctr_value_type, max_elems>;

    using alloc_ctr_value_type
            = detail::fast_atomic_uint_for_maxval<num_elems_per_block>;
    using alloc_ctr_atomic_type
            = detail::atomic_resource_counter<alloc_ctr_value_type,
                                              num_elems_per_block>;

    using atomic_byte_ptr = std::atomic<std::byte *>;

    using load_ctrs_t = std::array<alloc_ctr_atomic_type, num_blocks>;

    using alloc_map_t = pool_alloc_map_mt<num_elems_per_block>;
    using alloc_maps_t = std::array<alloc_map_t, num_blocks>;

    using blocks_t = std::array<atomic_byte_ptr, num_blocks>;

    static constexpr auto init_load_ctr(std::size_t i) noexcept
            -> alloc_ctr_atomic_type;

    static constexpr auto init_block(block_allocator_t &blockAllocator,
                                     std::size_t i) -> atomic_byte_ptr;

    inline auto block(std::size_t idx) -> rw_blob<alloc_block_size>;

public:
    // #MSVC_workaround #init_sequence
#if BOOST_COMP_MSVC < BOOST_VERSION_NUMBER(20, 0, 0)
    inline multi_pool_allocator_mt();
#else
    constexpr multi_pool_allocator_mt();
#endif
    inline ~multi_pool_allocator_mt();

    auto intr_allocate(const std::size_t size) noexcept
            -> result<std::tuple<memory_allocation, std::size_t>>;

    inline auto allocate(const std::size_t size) -> allocation_result;

private:
    inline auto block_info_of(std::byte *ptr) noexcept
            -> std::tuple<std::ptrdiff_t, std::size_t>;

public:
    inline bool owns(memory_allocation memory) noexcept;

    inline auto reallocate(memory_allocation memory,
                           const std::size_t newSize) noexcept
            -> allocation_result;

    inline void deallocate(memory_allocation memory) noexcept;

private:
    galloc_ctr_atomic_type mLoadCtr;
    load_ctrs_t mLoadCtrs;
    alloc_maps_t mAllocMaps;
    blocks_t mBlocks;
};

// #MSVC_workaround #init_sequence
#if BOOST_COMP_MSVC < BOOST_VERSION_NUMBER(20, 0, 0)
template <std::size_t ELEM_SIZE,
          std::size_t NUM_ELEMS_PER_BLOCK,
          std::size_t NUM_BLOCKS,
          typename BlockAllocator,
          std::size_t ALIGNMENT>
inline multi_pool_allocator_mt<ELEM_SIZE,
                               NUM_ELEMS_PER_BLOCK,
                               NUM_BLOCKS,
                               BlockAllocator,
                               ALIGNMENT>::multi_pool_allocator_mt()
    : mLoadCtr{detail::resource_is_initialized}
    , mLoadCtrs{alloc_ctr_atomic_type{resource_is_initialized}}
    , mAllocMaps{}
    , mBlocks{}
{
    if constexpr (num_blocks > 0)
    {

        if (allocation_result mx
            = block_allocator_t::allocate(alloc_block_size);
            mx.has_error())
        {
            if (mx.assume_error() == errc::not_enough_memory)
            {
                BOOST_THROW_EXCEPTION(std::bad_alloc{});
            }
            throw error_exception{std::move(mx).assume_error()};
        }
        else
        {
            std::atomic_init(&mBlocks.front(),
                             mx.assume_value().writeable_bytes().data());
        }
        for (std::size_t i = 1; i < mBlocks.size(); ++i)
        {
            std::atomic_init(&mBlocks[i], nullptr);
        }
    }
}
#else
template <std::size_t ELEM_SIZE,
          std::size_t NUM_ELEMS_PER_BLOCK,
          std::size_t NUM_BLOCKS,
          typename BlockAllocator,
          std::size_t ALIGNMENT>
constexpr auto
multi_pool_allocator_mt<ELEM_SIZE,
                        NUM_ELEMS_PER_BLOCK,
                        NUM_BLOCKS,
                        BlockAllocator,
                        ALIGNMENT>::init_load_ctr(std::size_t i) noexcept
        -> alloc_ctr_atomic_type
{
    return i == 0 ? alloc_ctr_atomic_type{resource_is_initialized}
                  : alloc_ctr_atomic_type{};
}

template <std::size_t ELEM_SIZE,
          std::size_t NUM_ELEMS_PER_BLOCK,
          std::size_t NUM_BLOCKS,
          typename BlockAllocator,
          std::size_t ALIGNMENT>
constexpr auto
multi_pool_allocator_mt<ELEM_SIZE,
                        NUM_ELEMS_PER_BLOCK,
                        NUM_BLOCKS,
                        BlockAllocator,
                        ALIGNMENT>::init_block(block_allocator_t
                                                       &blockAllocator,
                                               std::size_t i) -> atomic_byte_ptr
{
    if (i == 0)
    {
        allocation_result mx = blockAllocator.allocate(alloc_block_size);
        if (mx.has_error())
        {
            if (mx.assume_error() == errc::not_enough_memory)
            {
                BOOST_THROW_EXCEPTION(std::bad_alloc{});
            }
            throw error_exception{std::move(mx).assume_error()};
        }
        return atomic_byte_ptr{mx.assume_value().writeable_bytes().data()};
    }
    else
    {
        return atomic_byte_ptr{nullptr};
    }
}

template <std::size_t ELEM_SIZE,
          std::size_t NUM_ELEMS_PER_BLOCK,
          std::size_t NUM_BLOCKS,
          typename BlockAllocator,
          std::size_t ALIGNMENT>
constexpr multi_pool_allocator_mt<ELEM_SIZE,
                                  NUM_ELEMS_PER_BLOCK,
                                  NUM_BLOCKS,
                                  BlockAllocator,
                                  ALIGNMENT>::multi_pool_allocator_mt()
    : mLoadCtr{detail::resource_is_initialized}
    , mLoadCtrs{utils::sequence_init<load_ctrs_t, num_blocks>(init_load_ctr)}
    , mAllocMaps{}
    , mBlocks{utils::sequence_init<blocks_t, num_blocks>(
              init_block, *static_cast<block_allocator_t *>(this))}
{
}
#endif

template <std::size_t ELEM_SIZE,
          std::size_t NUM_ELEMS_PER_BLOCK,
          std::size_t NUM_BLOCKS,
          typename BlockAllocator,
          std::size_t ALIGNMENT>
inline auto multi_pool_allocator_mt<ELEM_SIZE,
                                    NUM_ELEMS_PER_BLOCK,
                                    NUM_BLOCKS,
                                    BlockAllocator,
                                    ALIGNMENT>::block(std::size_t idx)
        -> rw_blob<alloc_block_size>
{
    return {mBlocks[idx].load(std::memory_order_acquire), alloc_block_size};
}

template <std::size_t ELEM_SIZE,
          std::size_t NUM_ELEMS_PER_BLOCK,
          std::size_t NUM_BLOCKS,
          typename BlockAllocator,
          std::size_t ALIGNMENT>
inline multi_pool_allocator_mt<ELEM_SIZE,
                               NUM_ELEMS_PER_BLOCK,
                               NUM_BLOCKS,
                               BlockAllocator,
                               ALIGNMENT>::~multi_pool_allocator_mt()
{
    for (auto i = 0; i < num_blocks; ++i)
    {
        if (auto block = mBlocks[i].load(std::memory_order_acquire))
        {
            block_allocator_t::deallocate({block, alloc_block_size});
        }
    }
}

template <std::size_t ELEM_SIZE,
          std::size_t NUM_ELEMS_PER_BLOCK,
          std::size_t NUM_BLOCKS,
          typename BlockAllocator,
          std::size_t ALIGNMENT>
auto multi_pool_allocator_mt<ELEM_SIZE,
                             NUM_ELEMS_PER_BLOCK,
                             NUM_BLOCKS,
                             BlockAllocator,
                             ALIGNMENT>::intr_allocate(const std::size_t
                                                               size) noexcept
        -> result<std::tuple<memory_allocation, std::size_t>>
{
    if (size > avals::elem_size)
    {
        return errc::not_supported;
    }

    if (mLoadCtr.try_aquire_one() != detail::resource_acquire_result::success)
    {
        return errc::not_enough_memory;
    }

    auto i = 0;
    resource_acquire_result acquire_result;
    do
    {
        acquire_result = mLoadCtrs[i].try_aquire_one();
        switch (acquire_result)
        {
        case resource_acquire_result::do_init:
            if (allocation_result nextBlock
                = block_allocator_t::allocate(alloc_block_size);
                !nextBlock.has_error())
            {
                auto &mem = nextBlock.assume_value();
                mBlocks[i].store(mem.writeable_bytes().data(),
                                 std::memory_order_release);

                mLoadCtrs[i].notify_initialized();
            }
            else
            {
                mLoadCtrs[i].notify_cleanup_done();
                [[maybe_unused]] auto rls = mLoadCtr.release_one(false);
                return std::move(nextBlock).assume_error();
            }

        case resource_acquire_result::success:
            break;

        case resource_acquire_result::failure:
            if (++i == num_blocks)
            {
                i = 0;
            }
            break;
        }

    } while (acquire_result == resource_acquire_result::failure);

    auto blockPos = mAllocMaps[i].reserve_slot();
    auto memory = block(i).subspan(blockPos * avals::adj_elem_size, size);

    return std::tuple{memory_allocation{memory.data(), memory.size()},
                      blockPos};
}

template <std::size_t ELEM_SIZE,
          std::size_t NUM_ELEMS_PER_BLOCK,
          std::size_t NUM_BLOCKS,
          typename BlockAllocator,
          std::size_t ALIGNMENT>
inline auto multi_pool_allocator_mt<ELEM_SIZE,
                                    NUM_ELEMS_PER_BLOCK,
                                    NUM_BLOCKS,
                                    BlockAllocator,
                                    ALIGNMENT>::allocate(const std::size_t size)
        -> allocation_result
{
    VEFS_TRY(auto &&alloc, intr_allocate(size));
    return std::get<memory_allocation>(alloc);
}

template <std::size_t ELEM_SIZE,
          std::size_t NUM_ELEMS_PER_BLOCK,
          std::size_t NUM_BLOCKS,
          typename BlockAllocator,
          std::size_t ALIGNMENT>
inline auto
multi_pool_allocator_mt<ELEM_SIZE,
                        NUM_ELEMS_PER_BLOCK,
                        NUM_BLOCKS,
                        BlockAllocator,
                        ALIGNMENT>::block_info_of(std::byte *ptr) noexcept
        -> std::tuple<std::ptrdiff_t, std::size_t>
{
    if (ptr == nullptr)
    {
        return {-1, 0};
    }

    for (auto i = 0; i < num_blocks; ++i)
    {
        if (auto blockPtr = block(i).data();
            blockPtr && blockPtr <= ptr && ptr < blockPtr + alloc_block_size)
        {
            auto offset = ptr - blockPtr;
            assert(offset % avals::adj_elem_size == 0);
            return {i, offset / avals::adj_elem_size};
        }
    }
    return {-1, 0};
}

template <std::size_t ELEM_SIZE,
          std::size_t NUM_ELEMS_PER_BLOCK,
          std::size_t NUM_BLOCKS,
          typename BlockAllocator,
          std::size_t ALIGNMENT>
inline bool
multi_pool_allocator_mt<ELEM_SIZE,
                        NUM_ELEMS_PER_BLOCK,
                        NUM_BLOCKS,
                        BlockAllocator,
                        ALIGNMENT>::owns(memory_allocation memory) noexcept
{
    return memory.size() <= avals::elem_size
        && std::get<0>(
                   block_info_of(reinterpret_cast<std::byte *>(memory.raw())))
                   != -1;
}

template <std::size_t ELEM_SIZE,
          std::size_t NUM_ELEMS_PER_BLOCK,
          std::size_t NUM_BLOCKS,
          typename BlockAllocator,
          std::size_t ALIGNMENT>
inline auto
multi_pool_allocator_mt<ELEM_SIZE,
                        NUM_ELEMS_PER_BLOCK,
                        NUM_BLOCKS,
                        BlockAllocator,
                        ALIGNMENT>::reallocate(memory_allocation memory,
                                               const std::size_t
                                                       newSize) noexcept
        -> allocation_result
{
    assert(owns(memory));

    if (newSize > avals::elem_size)
    {
        return errc::not_supported;
    }
    return memory_allocation{memory.writeable_bytes().data(), newSize};
}

template <std::size_t ELEM_SIZE,
          std::size_t NUM_ELEMS_PER_BLOCK,
          std::size_t NUM_BLOCKS,
          typename BlockAllocator,
          std::size_t ALIGNMENT>
inline void
multi_pool_allocator_mt<ELEM_SIZE,
                        NUM_ELEMS_PER_BLOCK,
                        NUM_BLOCKS,
                        BlockAllocator,
                        ALIGNMENT>::deallocate(memory_allocation
                                                       memory) noexcept
{
    assert(memory.size() <= avals::elem_size);
    auto ptr = memory.writeable_bytes().data();
    auto [blockIdx, blockPos] = block_info_of(ptr);
    assert(blockIdx != -1);

    mAllocMaps[blockIdx].release_slot(blockPos);

    // blockIdx > 0 => the first block is never released
    switch (mLoadCtrs[blockIdx].release_one(num_blocks > 1 && blockIdx > 0))
    {
    case resource_release_result::do_cleanup:
    {
        auto blockPtr = mBlocks[blockIdx].exchange(nullptr,
                                                   std::memory_order_acq_rel);
        block_allocator_t::deallocate({blockPtr, alloc_block_size});

        mLoadCtrs[blockIdx].notify_cleanup_done();
        break;
    }

    case resource_release_result::success:
        break;
    }

    [[maybe_unused]] auto rls = mLoadCtr.release_one(false);
}
} // namespace vefs::detail
