#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <thread>

#include <vefs/utils/allocator.hpp>
#include <vefs/utils/misc.hpp>
#include <vefs/utils/bit_scan.hpp>

namespace vefs::utils::detail
{
    template <std::size_t N_BITS>
    using fast_atomic_uint_with_bits
        = std::conditional_t<N_BITS <= std::numeric_limits<unsigned int>::digits,
        unsigned int, std::size_t>;

    template <std::size_t MAX_VALUE>
    using fast_atomic_uint_for_maxval
        = std::conditional_t<MAX_VALUE <= std::numeric_limits<unsigned int>::max(),
        unsigned int, std::size_t>;

    template <std::size_t LIMIT>
    class atomic_ring_counter
    {
        static constexpr std::size_t limit = LIMIT;
        static_assert(limit > 0);

        using value_type = fast_atomic_uint_for_maxval<limit - 1>;
        using atomic_type = std::atomic< value_type >;

    public:
        constexpr atomic_ring_counter()
            : mCtr{ 0 }
        {
        }

        inline std::size_t fetch_next()
        {
            const auto nxt = mCtr.fetch_add(1, std::memory_order_acquire);
            if constexpr (limit - 1 == static_cast<std::size_t>(std::numeric_limits<value_type>::max()))
            {
                return nxt;
            }
            else
            {
                return nxt % limit;
            }
        }

    private:
        atomic_type mCtr;
    };

    template <>
    class atomic_ring_counter<1>
    {
    public:
        static constexpr std::size_t fetch_next()
        {
            return 0;
        }
    };

    enum class resource_acquire_result
    {
        failure = 0,
        success = 1,
        do_init = -1,
    };

    enum class resource_release_result
    {
        success = 0,
        do_cleanup = -1,
    };

    enum class resource_is_initialized_t {};
    constexpr resource_is_initialized_t resource_is_initialized{};

    template <typename counter_type, counter_type LIMIT>
    class atomic_resource_counter
    {
        using value_type = counter_type;
        using value_type_limits = std::numeric_limits<value_type>;
        using atomic_type = std::atomic<value_type>;

        static constexpr value_type limit = LIMIT;
        static constexpr value_type uninitialized = value_type_limits::max();
        static constexpr value_type initializing = uninitialized - 1;
        static constexpr value_type deinitializing = initializing - 1;

        static_assert(limit < deinitializing);

    public:
        constexpr atomic_resource_counter()
            : mState{ uninitialized }
        {
        }
        constexpr atomic_resource_counter(resource_is_initialized_t)
            : mState{ 0 }
        {
        }


        [[nodiscard]]
        inline resource_acquire_result try_aquire_one()
        {
            auto value = mState.load(std::memory_order_acquire);
            value_type next;
            do
            {
                if (value == limit)
                {
                    return resource_acquire_result::failure;
                }
                else if (value == uninitialized)
                {
                    next = initializing;
                }
                else if (value == initializing)
                {
                    if constexpr (limit == 1)
                    {
                        return resource_acquire_result::failure;
                    }
                    else
                    {
                        value = 1;
                        next = 2;
                        std::this_thread::yield();
                    }
                }
                else if (value == deinitializing)
                {
                    value = uninitialized;
                    next = initializing;
                    std::this_thread::yield();
                }
                else
                {
                    next = value + 1;
                }

            } while (!mState.compare_exchange_weak(value, next,
                        std::memory_order_acq_rel, std::memory_order_acquire));

            return next == initializing
                ? resource_acquire_result::do_init
                : resource_acquire_result::success;
        }

        [[nodiscard]]
        inline resource_release_result release_one(bool deinitOnZero)
        {
            if (mState.fetch_sub(1, std::memory_order_release) == 1 && deinitOnZero)
            {
                value_type exp = 0;
                if (mState.compare_exchange_strong(exp, deinitializing,
                    std::memory_order_acq_rel))
                {
                    return resource_release_result::do_cleanup;
                }
            }
            return resource_release_result::success;
        }

        inline void notify_initialized()
        {
            mState.store(1, std::memory_order_release);
        }

        inline void notify_cleanup_done()
        {
            mState.store(uninitialized, std::memory_order_release);
        }

    private:
        atomic_type mState;
    };

    template <std::size_t NUM_ELEMS, typename unit_type = fast_atomic_uint_with_bits<NUM_ELEMS>>
    class pool_alloc_map_mt
        : private atomic_ring_counter< div_ceil<std::size_t>(NUM_ELEMS, std::numeric_limits<unit_type>::digits) >
    {
        static_assert(std::is_integral_v<unit_type> && std::is_unsigned_v<unit_type>);

        using unit_impl_t = unit_type;
        using unit_impl_limits = std::numeric_limits<unit_impl_t>;
        using unit_t = std::atomic<unit_impl_t>;

        static constexpr std::size_t num_elems = NUM_ELEMS;
        static constexpr std::size_t elems_per_unit = unit_impl_limits::digits;
        static constexpr std::size_t num_units = div_ceil(num_elems, elems_per_unit);

        using ring_counter_base = atomic_ring_counter<num_units>;

        template <typename T>
        static constexpr auto bit_at(T shift)
        {
            return unit_impl_t{ 1 } << shift;
        }

        static constexpr auto unit_init_state = unit_impl_limits::max();
        static constexpr auto compute_last_unit_init_state()
        {
            auto mask = unit_init_state;

            constexpr auto numEntriesLastBlock = num_elems % elems_per_unit;
            if constexpr (numEntriesLastBlock != 0)
            {
                mask = 1;
                for (auto i = 1; i < numEntriesLastBlock; ++i)
                {
                    mask |= bit_at(i);
                }
            }
            return mask;
        }
        static constexpr auto last_unit_init_state = compute_last_unit_init_state();

    public:
        static constexpr auto failed_reservation = std::numeric_limits<std::size_t>::max();

        inline pool_alloc_map_mt()
            : ring_counter_base{}
            , mAllocMap{}
        {
            if constexpr (num_units > 1)
            {
                for (auto i = 0; i < num_units - 1; ++i)
                {
                    std::atomic_init(&mAllocMap[i], unit_init_state);
                }
            }
            std::atomic_init(&mAllocMap[num_units - 1], last_unit_init_state);
        }

        std::size_t reserve_slot()
        {
            std::size_t pos;
            std::size_t unitIdx;
            do
            {
                unitIdx = this->fetch_next();

                auto &blockRef = mAllocMap[unitIdx];
                auto unitVal = blockRef.load(std::memory_order_acquire);
                do
                {
                    if (!bit_scan(pos, unitVal))
                    {
                        // this block is empty, try next one
                        pos = failed_reservation;
                        break;
                    }
                } while (!blockRef.compare_exchange_weak(unitVal, unitVal ^ bit_at(pos),
                    std::memory_order_acq_rel, std::memory_order_acquire));

            } while (pos == failed_reservation);

            return unitIdx * elems_per_unit + pos;
        }

        void release_slot(std::size_t slot)
        {
            const auto unitIdx = slot / elems_per_unit;
            const auto pos = slot % elems_per_unit;

            mAllocMap[unitIdx].fetch_or(bit_at(pos), std::memory_order_release);
        }

    private:
        std::array<unit_t, num_units> mAllocMap;
    };
}

namespace vefs::utils
{
    template <std::size_t ELEM_SIZE, std::size_t NUM_ELEMS, typename BlockAllocator, std::size_t ALIGNMENT = BlockAllocator::alignment>
    class pool_allocator_mt
        : private BlockAllocator
        , private detail::alignmnent_values<ELEM_SIZE, ALIGNMENT>
    {
        using block_allocator_t = BlockAllocator;
        using cblock_unit_t = std::atomic<std::size_t>;
        using avals = detail::alignmnent_values<ELEM_SIZE, ALIGNMENT>;

    public:
        static constexpr bool is_thread_safe = true;
        static constexpr std::size_t alignment = ALIGNMENT;
        static_assert(block_allocator_t::alignment % alignment == 0, "the underlying block allocator must provide an already aligned block");

    private:
        static constexpr std::size_t num_elems = NUM_ELEMS;
        static constexpr std::size_t alloc_block_size = num_elems * avals::adj_elem_size;

        using alloc_ctr_value_type = detail::fast_atomic_uint_for_maxval<num_elems>;
        using alloc_ctr_atomic_type = detail::atomic_resource_counter<alloc_ctr_value_type, num_elems>;

    public:
        inline pool_allocator_mt()
            : block_allocator_t()
            , mBlockPtr{ nullptr }
            , mAllocCtr{ detail::resource_is_initialized }
            , mAllocMap{ }
        {
            maybe_allocation block = block_allocator_t::allocate(alloc_block_size);
            if (block == failed_allocation)
            {
                BOOST_THROW_EXCEPTION(std::bad_alloc{});
            }
            mBlockPtr = reinterpret_cast<std::byte *>(block.value().raw());
        }
        inline ~pool_allocator_mt()
        {
            block_allocator_t::deallocate({ mBlockPtr, alloc_block_size });
        }

        [[nodiscard]]
        inline std::tuple<maybe_allocation, std::size_t> intr_allocate(const std::size_t size)
        {
            if (size > avals::elem_size)
            {
                return { failed_allocation, 0 };
            }

            if (mAllocCtr.try_aquire_one() != detail::resource_acquire_result::success)
            {
                return { failed_allocation, 0 };
            }

            auto blockPos = mAllocMap.reserve_slot();
            return { {{mBlockPtr + blockPos * avals::adj_elem_size, size}}, blockPos };
        }

        [[nodiscard]]
        inline maybe_allocation allocate(const std::size_t size)
        {
            return std::get<maybe_allocation>(intr_allocate(size));
        }

        [[nodiscard]]
        inline maybe_allocation reallocate(const memory_allocation memblock, const std::size_t size)
        {
            assert(owns(memblock));

            if (size > avals::elem_size)
            {
                return failed_allocation;
            }
            return { reinterpret_cast<std::byte *>(memblock.raw()), size };
        }

        inline void deallocate(memory_allocation memblock)
        {
            assert(owns(memblock));

            auto *const ptr = reinterpret_cast<std::byte *>(memblock.raw());
            const auto idx = (ptr - mBlockPtr) / avals::adj_elem_size;

            mAllocMap.release_slot(idx);

            [[maybe_unused]] auto action = mAllocCtr.release_one(false);
        }

        inline bool owns(const memory_allocation memblock) const noexcept
        {
            std::byte *start = reinterpret_cast<std::byte *>(memblock.raw());
            std::byte *end = reinterpret_cast<std::byte *>(memblock.raw_end());
            std::byte *my_end = mBlockPtr + alloc_block_size;
            return mBlockPtr <= start && end <= my_end
                && ((start - mBlockPtr) % avals::adj_elem_size) == 0
                && memblock.size() <= avals::elem_size;
        }

    private:
        std::byte *mBlockPtr;

        alloc_ctr_atomic_type mAllocCtr;
        detail::pool_alloc_map_mt<num_elems> mAllocMap;
    };

    template <std::size_t ELEM_SIZE, std::size_t NUM_ELEMS_PER_BLOCK, std::size_t NUM_BLOCKS,
        typename BlockAllocator, std::size_t ALIGNMENT = BlockAllocator::alignment >
        class multi_pool_allocator_mt
        : private BlockAllocator
        , private detail::alignmnent_values<ELEM_SIZE, ALIGNMENT>
    {
        using block_allocator_t = BlockAllocator;
        //using cblock_unit_t = std::atomic<std::size_t>;
        using avals = detail::alignmnent_values<ELEM_SIZE, ALIGNMENT>;

    public:
        static constexpr bool is_thread_safe = true;
        static constexpr std::size_t alignment = ALIGNMENT;
        static_assert(block_allocator_t::alignment % alignment == 0, "the underlying block allocator must provide an already aligned block");

    private:
        static constexpr std::size_t num_elems_per_block = NUM_ELEMS_PER_BLOCK;
        static constexpr std::size_t num_blocks = NUM_BLOCKS;
        static constexpr std::size_t max_elems = num_elems_per_block * num_blocks;


        static constexpr std::size_t alloc_block_size = num_elems_per_block * avals::adj_elem_size;

        using galloc_ctr_value_type = detail::fast_atomic_uint_for_maxval<max_elems>;
        using galloc_ctr_atomic_type = detail::atomic_resource_counter<galloc_ctr_value_type, max_elems>;

        using alloc_ctr_value_type = detail::fast_atomic_uint_for_maxval<num_elems_per_block>;
        using alloc_ctr_atomic_type = detail::atomic_resource_counter<alloc_ctr_value_type, num_elems_per_block>;

        using atomic_byte_ptr = std::atomic<std::byte *>;

    public:
        inline multi_pool_allocator_mt()
            : mLoadCtr{ detail::resource_is_initialized }
            , mLoadCtrs{}
            , mAllocMaps{}
            , mBlocks{}
        {
            // we always allocate the first block on init and keep it allocated afterwards
            maybe_allocation fstBlock = block_allocator_t::allocate(alloc_block_size);
            if (fstBlock == failed_allocation)
            {
                BOOST_THROW_EXCEPTION(std::bad_alloc{});
            }
            std::atomic_init(&mBlocks[0], reinterpret_cast<std::byte *>(fstBlock->raw()));

            for (auto i = 1; i < num_blocks; ++i)
            {
                std::atomic_init(&mBlocks[i], nullptr);
            }

            auto &fstLoadCtr = mLoadCtrs[0];
            [[maybe_unused]] auto acquireResult = fstLoadCtr.try_aquire_one();
            assert(acquireResult == detail::resource_acquire_result::do_init);
            fstLoadCtr.notify_initialized();
            [[maybe_unused]] auto releaseResult = fstLoadCtr.release_one(false);
            assert(releaseResult == detail::resource_release_result::success);
        }
        inline ~multi_pool_allocator_mt()
        {
            for (auto i = 0; i < num_blocks; ++i)
            {
                if (auto block = mBlocks[i].load(std::memory_order_acquire))
                {
                    block_allocator_t::deallocate({ block, alloc_block_size });
                }
            }
        }

        [[nodiscard]]
        inline std::tuple<maybe_allocation, std::size_t> intr_allocate(const std::size_t size)
        {
            if (size > avals::elem_size)
            {
                return { failed_allocation, 0 };
            }

            if (mLoadCtr.try_aquire_one() != detail::resource_acquire_result::success)
            {
                return { failed_allocation, 0 };
            }

            auto i = 0;
            {
                detail::resource_acquire_result result;
                do
                {
                    result = mLoadCtrs[i].try_aquire_one();
                    switch (result)
                    {
                    case vefs::utils::detail::resource_acquire_result::do_init:
                        if (maybe_allocation nextBlock = block_allocator_t::allocate(alloc_block_size))
                        {
                            mBlocks[i].store(reinterpret_cast<std::byte *>(nextBlock->raw()),
                                std::memory_order_release);

                            mLoadCtrs[i].notify_initialized();
                        }
                        else
                        {
                            mLoadCtrs[i].notify_cleanup_done();
                            [[maybe_unused]] auto rls = mLoadCtr.release_one(false);
                            return { failed_allocation, 0 };
                        }

                    case vefs::utils::detail::resource_acquire_result::success:
                        break;

                    case vefs::utils::detail::resource_acquire_result::failure:
                        if (++i == num_blocks)
                        {
                            i = 0;
                        }
                        break;
                    }

                } while (result == detail::resource_acquire_result::failure);
            }

            auto blockPos = mAllocMaps[i].reserve_slot();

            auto blockPtr = mBlocks[i].load(std::memory_order_acquire);

            return { { { blockPtr + blockPos * avals::adj_elem_size, size } }, blockPos };
        }

        [[nodiscard]]
        inline maybe_allocation allocate(const std::size_t size)
        {
            return std::get<maybe_allocation>(intr_allocate(size));
        }

    private:
        inline std::tuple<std::ptrdiff_t, std::size_t> block_info_of(std::byte *ptr)
        {
            if (ptr == nullptr)
            {
                return { -1, 0 };
            }

            for (auto i = 0; i < num_blocks; ++i)
            {
                if (auto blockPtr = mBlocks[i].load(std::memory_order_acquire);
                        blockPtr && blockPtr <= ptr && ptr < blockPtr + alloc_block_size)
                {
                    auto offset = ptr - blockPtr;
                    assert(offset % avals::adj_elem_size == 0);
                    return { i, offset / avals::adj_elem_size };
                }
            }
            return { -1, 0 };
        }

    public:
        inline bool owns(memory_allocation block)
        {
            return block.size() <= avals::elem_size
                && std::get<0>(block_info_of(reinterpret_cast<std::byte *>(block.raw()))) != -1;
        }

        [[nodiscard]]
        maybe_allocation reallocate(memory_allocation block, const std::size_t newSize)
        {
            assert(owns(block));

            return newSize <= avals::elem_size
                ? maybe_allocation{ block }
                : failed_allocation;
        }

        inline void deallocate(memory_allocation block)
        {
            assert(block.size() <= avals::elem_size);
            auto ptr = reinterpret_cast<std::byte *>(block.raw());
            auto [blockIdx, blockPos] = block_info_of(ptr);
            assert(blockIdx != -1);

            mAllocMaps[blockIdx].release_slot(blockPos);

            switch (mLoadCtrs[blockIdx].release_one(num_blocks > 1 && blockIdx > 0))
            {
            case detail::resource_release_result::do_cleanup:
            {
                auto blockPtr = mBlocks[blockIdx].exchange(nullptr, std::memory_order_acq_rel);
                block_allocator_t::deallocate({ blockPtr, alloc_block_size });

                mLoadCtrs[blockIdx].notify_cleanup_done();
            }
                break;

            case detail::resource_release_result::success:
                break;
            }

            [[maybe_unused]] auto rls = mLoadCtr.release_one(false);
        }

    private:
        galloc_ctr_atomic_type mLoadCtr;
        std::array<alloc_ctr_atomic_type, num_blocks> mLoadCtrs;
        std::array<detail::pool_alloc_map_mt<num_elems_per_block>, num_blocks> mAllocMaps;
        std::array<atomic_byte_ptr, num_blocks> mBlocks;
    };
}
