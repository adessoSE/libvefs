#pragma once

#include <cstddef>
#include <cstdint>

#include <atomic>
#include <limits>
#include <type_traits>

namespace vefs::detail
{
template <std::size_t N_BITS>
using fast_atomic_uint_with_bits = std::conditional_t<
        N_BITS <= std::numeric_limits<unsigned int>::digits,
        unsigned int,
        std::size_t>;

template <std::size_t MAX_VALUE>
using fast_atomic_uint_for_maxval = std::conditional_t<
        MAX_VALUE <= std::numeric_limits<unsigned int>::max(),
        unsigned int,
        std::size_t>;

template <std::size_t LIMIT>
class atomic_ring_counter
{
    static constexpr std::size_t limit = LIMIT;
    static_assert(limit > 0);

    using value_type = fast_atomic_uint_for_maxval<limit - 1>;
    using atomic_type = std::atomic<value_type>;

public:
    constexpr atomic_ring_counter() noexcept;

    inline auto fetch_next() noexcept -> std::size_t;

private:
    atomic_type mCtr;
};

template <std::size_t LIMIT>
constexpr atomic_ring_counter<LIMIT>::atomic_ring_counter() noexcept
    : mCtr{0}
{
}

template <std::size_t LIMIT>
inline auto atomic_ring_counter<LIMIT>::fetch_next() noexcept -> std::size_t
{
    const auto nxt = mCtr.fetch_add(1, std::memory_order_acquire);
    if constexpr (limit - 1
                  == static_cast<std::size_t>(
                          std::numeric_limits<value_type>::max()))
    {
        return nxt;
    }
    else
    {
        return nxt % limit;
    }
}

template <>
class atomic_ring_counter<1>
{
public:
    constexpr atomic_ring_counter() noexcept = default;

    inline auto fetch_next() noexcept -> std::size_t;
};

inline auto atomic_ring_counter<1>::fetch_next() noexcept -> std::size_t
{
    return 0;
}
} // namespace vefs::detail
