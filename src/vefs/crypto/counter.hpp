#pragma once

#include <cassert>
#include <climits>
#include <cstddef>

#include <atomic>
#include <bit>
#include <type_traits>

#include <dplx/dp.hpp>
#include <dplx/dp/macros.hpp>

#include <vefs/span.hpp>

#include <boost/predef/architecture.h>
#include <boost/predef/compiler.h>
#include <boost/predef/os.h>

#define VEFS_COUNTER_IMPL_GENERIC     0b00010
#define VEFS_COUNTER_IMPL_INT128      0b00101
#define VEFS_COUNTER_IMPL_ADC64       0b01001
#define VEFS_COUNTER_IMPL_ADC32       0b10000
#define VEFS_COUNTER_IMPL_ULL_STORAGE 0b00001

#if defined(__SIZEOF_INT128__)
#define VEFS_COUNTER_IMPL VEFS_COUNTER_IMPL_INT128
#elif defined(BOOST_ARCH_X86_AVAILABLE)
#if defined(BOOST_COMP_GNUC_AVAILABLE)
#include <x86intrin.h>
#else
#include <intrin.h.>
#endif
#if defined(BOOST_ARCH_X86_64_AVAILABLE)
#define VEFS_COUNTER_IMPL VEFS_COUNTER_IMPL_ADC64
#else
#define VEFS_COUNTER_IMPL VEFS_COUNTER_IMPL_ADC32
#endif
#else
#define VEFS_COUNTER_IMPL VEFS_COUNTER_IMPL_GENERIC
#endif

namespace vefs::crypto
{

class counter
{
public:
    struct state
    {
// we explicitly reference __x86_64__ to account for the x32 ABI
#if VEFS_COUNTER_IMPL & VEFS_COUNTER_IMPL_ULL_STORAGE
        unsigned long long value[2];

        friend inline constexpr auto as_span(state &s) noexcept
                -> std::span<unsigned long long, 2>
        {
            return {s.value};
        }
        friend inline constexpr auto as_span(state const &s) noexcept
                -> std::span<unsigned long long const, 2>
        {
            return {s.value};
        }
#else
        std::uint32_t value[4];

        friend inline constexpr auto as_span(state &s) noexcept
                -> std::span<std::uint32_t, 4>
        {
            return {s.value};
        }
        friend inline constexpr auto as_span(state const &s) noexcept
                -> std::span<std::uint32_t const, 4>
        {
            return {s.value};
        }
#endif
        friend constexpr auto operator==(state const &lhs,
                                         state const &rhs) noexcept -> bool
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
                = default;
    };

private:
    state mCtrState{};

public:
    // inline ~counter() noexcept
    // {
    //     utils::secure_memzero(as_blob());
    // }
    constexpr counter() noexcept = default;

    constexpr counter(counter const &) = default;
    constexpr counter &operator=(counter const &) = default;
    constexpr counter(counter &&) noexcept = default;
    constexpr counter &operator=(counter &&) noexcept = default;

    explicit constexpr counter(state ctrState) noexcept
        : mCtrState(ctrState)
    {
    }
    explicit inline counter(ro_blob<sizeof(state)> ctrState) noexcept
    {
        copy(ctrState, as_blob());
    }

    static constexpr std::size_t state_size{16};
    static_assert(sizeof(state) == state_size);

    [[nodiscard]] inline auto value() const noexcept -> state const &
    {
        return mCtrState;
    }
    [[nodiscard]] inline auto view() const noexcept -> ro_blob<sizeof(state)>
    {
        return as_bytes(std::span(mCtrState.value));
    }
    inline void increment() noexcept
    {
#if VEFS_COUNTER_IMPL == VEFS_COUNTER_IMPL_INT128
        mCtrState = std::bit_cast<state>(
                __extension__ std::bit_cast<unsigned __int128>(mCtrState) + 1);
#elif VEFS_COUNTER_IMPL == VEFS_COUNTER_IMPL_ADC64
        _addcarry_u64(_addcarry_u64(0U, mCtrState.value[0], 1ULL,
                                    &mCtrState.value[0]),
                      mCtrState.value[1], 0, &mCtrState.value[1]);
#elif VEFS_COUNTER_IMPL == VEFS_COUNTER_IMPL_ADC32
        _addcarry_u32(
                _addcarry_u32(
                        _addcarry_u32(_addcarry_u32(0U, mCtrState.value[0], 1,
                                                    &mCtrState.value[0]),
                                      mCtrState.value[1], 0,
                                      &mCtrState.value[1]),
                        mCtrState.value[2], 0, &mCtrState.value[2]),
                mCtrState.value[3], 0, &mCtrState.value[3]);
#elif VEFS_COUNTER_IMPL == VEFS_COUNTER_IMPL_GENERIC
        constexpr int bitWidth = sizeof(std::uint32_t) * CHAR_BIT;
        std::uint64_t acc{1};
        acc += mCtrState.value[0];
        mCtrState.value[0] = static_cast<std::uint32_t>(acc);
        acc = mCtrState.value[1] + (acc >> bitWidth);
        mCtrState.value[1] = static_cast<std::uint32_t>(acc);
        acc = mCtrState.value[2] + (acc >> bitWidth);
        mCtrState.value[2] = static_cast<std::uint32_t>(acc);
        acc = mCtrState.value[3] + (acc >> bitWidth);
        mCtrState.value[3] = static_cast<std::uint32_t>(acc);
#else
#error "invalid VEFS_COUNTER_IMPL value"
#endif
    }
    inline auto operator++() -> counter &
    {
        increment();
        return *this;
    }
    inline auto operator++(int) -> counter
    {
        counter original{*this};
        increment();
        return original;
    }

    friend constexpr auto operator==(counter const &lhs,
                                     counter const &rhs) noexcept -> bool
            = default;

private:
    [[nodiscard]] auto as_blob() noexcept -> rw_blob<sizeof(state)>
    {
        return as_writable_bytes(std::span(mCtrState.value));
    }
};

} // namespace vefs::crypto

#undef VEFS_COUNTER_IMPL
#undef VEFS_COUNTER_IMPL_ULL_STORAGE
#undef VEFS_COUNTER_IMPL_ADC32
#undef VEFS_COUNTER_IMPL_ADC64
#undef VEFS_COUNTER_IMPL_INT128
#undef VEFS_COUNTER_IMPL_GENERIC

DPLX_DP_DECLARE_CODEC_SIMPLE(vefs::crypto::counter);

template <>
struct std::atomic<vefs::crypto::counter>
{
private:
    using mutex_type =
#if defined(BOOST_OS_LINUX_AVAILABLE) || defined(BOOST_OS_ANDROID_AVAILABLE)   \
        || defined(BOOST_OS_WINDOWS_AVAILABLE)
            std::atomic<std::int32_t>
#else
            std::atomic<std::make_signed_t<std::size_t>>
#endif
            ;

    vefs::crypto::counter mImpl{};
    mutable mutex_type mAccessMutex{0};

public:
    using value_type = vefs::crypto::counter;

    constexpr atomic() noexcept = default;
    constexpr atomic(value_type ctr) noexcept
        : mImpl(ctr)
    {
    }
    explicit constexpr atomic(value_type::state ctrState) noexcept
        : mImpl(ctrState)
    {
    }
    explicit atomic(vefs::ro_blob<value_type::state_size> ctrState) noexcept
        : mImpl(ctrState)
    {
    }
    atomic(atomic const &) = delete;

    static constexpr auto is_lock_free() noexcept -> bool
    {
        return false;
    }
    static constexpr bool is_always_lock_free = false;

    void store(value_type desired,
               std::memory_order = std::memory_order_seq_cst) noexcept
    {
        lock_guard lock{mAccessMutex};
        mImpl = desired;
    }
    auto load(std::memory_order = std::memory_order_seq_cst) const noexcept
            -> value_type
    {
        lock_guard lock{mAccessMutex};
        return mImpl;
    }

    operator value_type() const noexcept
    {
        return load();
    }
    // the value assignment operator accepts and returns `T` as per C++ standard
    // NOLINTNEXTLINE(cppcoreguidelines-c-copy-assignment-signature)
    auto operator=(value_type desired) noexcept -> value_type
    {
        lock_guard lock{mAccessMutex};
        mImpl = desired;
        return desired;
    }
    auto operator=(atomic const &) -> atomic & = delete;

    auto exchange(value_type desired,
                  std::memory_order = std::memory_order_seq_cst) noexcept
            -> value_type
    {
        lock_guard lock{mAccessMutex};
        value_type old{mImpl};
        mImpl = desired;
        return old;
    }
    auto compare_exchange_weak(value_type const &expected,
                               value_type desired,
                               std::memory_order
                               = std::memory_order_seq_cst) noexcept -> bool
    {
        lock_guard lock{mAccessMutex};
        auto const success = mImpl == expected;
        if (success)
        {
            mImpl = desired;
        }
        return success;
    }
    auto compare_exchange_strong(value_type const &expected,
                                 value_type desired,
                                 std::memory_order
                                 = std::memory_order_seq_cst) noexcept -> bool
    {
        return compare_exchange_weak(expected, desired);
    }

    auto fetch_increment() noexcept -> value_type
    {
        lock_guard lock{mAccessMutex};
        return mImpl++;
    }
    auto operator++() noexcept -> value_type
    {
        lock_guard lock{mAccessMutex};
        return ++mImpl;
    }
    auto operator++(int) noexcept -> value_type
    {
        lock_guard lock{mAccessMutex};
        return mImpl++;
    }

private:
    struct [[nodiscard]] lock_guard
    {
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
        mutex_type &mMutex;

        static constexpr mutex_type::value_type unlocked_value{0};
        static constexpr mutex_type::value_type locked_value{1};

        lock_guard(mutex_type &mutex) noexcept
            : mMutex(mutex)
        {
            mutex_type::value_type expected{unlocked_value};
            while (!mMutex.compare_exchange_weak(expected, locked_value,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire))
            {
                mMutex.wait(expected, std::memory_order_acquire);
                expected = unlocked_value;
            }
        }
        ~lock_guard() noexcept
        {
            mMutex.store(unlocked_value, std::memory_order_release);
            mMutex.notify_one();
        }
    };
};

namespace vefs::crypto
{

using atomic_counter = std::atomic<counter>;

}

DPLX_DP_DECLARE_CODEC_SIMPLE(vefs::crypto::atomic_counter);
