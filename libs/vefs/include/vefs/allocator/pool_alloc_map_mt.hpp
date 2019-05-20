#pragma once

#include <cstddef>

#include <array>
#include <atomic>
#include <limits>
#include <type_traits>
#include <utility>

#include <vefs/allocator/atomic_ring_counter.hpp>
#include <vefs/utils/misc.hpp>
#include <vefs/utils/bit_scan.hpp>

namespace vefs::detail
{
    template <std::size_t NUM_ELEMS, typename unit_type = fast_atomic_uint_with_bits<NUM_ELEMS>>
    class pool_alloc_map_mt
        : private atomic_ring_counter<utils::div_ceil<std::size_t>(
              NUM_ELEMS, std::numeric_limits<unit_type>::digits)>
    {
        static_assert(std::is_integral_v<unit_type> && std::is_unsigned_v<unit_type>);

        using unit_impl_t = unit_type;
        using unit_impl_limits = std::numeric_limits<unit_impl_t>;

        static constexpr std::size_t num_elems = NUM_ELEMS;
        static constexpr std::size_t elems_per_unit = unit_impl_limits::digits;
        static constexpr std::size_t num_units = utils::div_ceil(num_elems, elems_per_unit);

        using unit_t = std::atomic<unit_impl_t>;
        using units_t = std::array<unit_t, num_units>;

        using ring_counter_base = atomic_ring_counter<num_units>;

        template <typename T>
        static constexpr auto bit_at(T shift)
        {
            return unit_impl_t{1} << shift;
        }

        static constexpr auto unit_init_state = unit_impl_limits::max();
        static constexpr auto last_unit_init_state = ([]() constexpr {
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
        })();

        static constexpr auto init_alloc_map(std::size_t i) noexcept -> unit_t;

    public:
        static constexpr auto failed_reservation = std::numeric_limits<std::size_t>::max();

        // #MSVC_workaround #init_sequence
#if BOOST_COMP_MSVC < BOOST_VERSION_NUMBER(20, 0, 0)
        inline pool_alloc_map_mt() noexcept;
#else
        constexpr pool_alloc_map_mt() noexcept;
#endif

        inline auto reserve_slot() noexcept -> std::size_t;
        void release_slot(std::size_t slot) noexcept;

    private:
        units_t mAllocMap;
    };

    // #MSVC_workaround #init_sequence
#if BOOST_COMP_MSVC < BOOST_VERSION_NUMBER(20, 0, 0)
    template <std::size_t NUM_ELEMS, typename unit_type>
    inline pool_alloc_map_mt<NUM_ELEMS, unit_type>::pool_alloc_map_mt() noexcept
        : ring_counter_base{}
        , mAllocMap{}
    {
        if constexpr (num_units > 0)
        {
            for (std::size_t i = 0; i < mAllocMap.size() - 1; ++i)
            {
                std::atomic_init(&mAllocMap[i], unit_init_state);
            }
            std::atomic_init(&mAllocMap.back(), last_unit_init_state);
        }
    }
#else
    template <std::size_t NUM_ELEMS, typename unit_type>
    constexpr auto pool_alloc_map_mt<NUM_ELEMS, unit_type>::init_alloc_map(std::size_t i) noexcept
        -> unit_t
    {
        // this function is explicitly _not_ a lambda, because this would cause
        // an exploding amount of instantiations

        return unit_t{i == num_units - 1 ? last_unit_init_state : unit_init_state};
    }

    template <std::size_t NUM_ELEMS, typename unit_type>
    constexpr pool_alloc_map_mt<NUM_ELEMS, unit_type>::pool_alloc_map_mt() noexcept
        : ring_counter_base{}
        , mAllocMap{utils::sequence_init<units_t, num_units>(init_alloc_map)}
    {
    }
#endif // BOOST_COMP_MSVC < BOOST_VERSION_NUMBER(20, 0, 0)

    template <std::size_t NUM_ELEMS, typename unit_type>
    inline auto pool_alloc_map_mt<NUM_ELEMS, unit_type>::reserve_slot() noexcept -> std::size_t
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
                if (!utils::bit_scan(pos, unitVal))
                {
                    // this block is empty, try next one
                    pos = failed_reservation;
                    break;
                }
            } while (!blockRef.compare_exchange_weak(unitVal, unitVal ^ bit_at(pos),
                                                     std::memory_order_acq_rel,
                                                     std::memory_order_acquire));

        } while (pos == failed_reservation);

        return unitIdx * elems_per_unit + pos;
    }
    template <std::size_t NUM_ELEMS, typename unit_type>
    inline void pool_alloc_map_mt<NUM_ELEMS, unit_type>::release_slot(std::size_t slot) noexcept
    {
        const auto unitIdx = slot / elems_per_unit;
        const auto pos = slot % elems_per_unit;

        mAllocMap[unitIdx].fetch_or(bit_at(pos), std::memory_order_release);
    }

} // namespace vefs::detail
