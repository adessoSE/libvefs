#include "precompiled.hpp"
#include <vefs/crypto/counter.hpp>

#include <boost/predef.h>

#if defined BOOST_COMP_MSVC_AVAILABLE
#include <intrin.h>
#elif defined BOOST_COMP_GCC_AVAILABLE || defined BOOST_COMP_CLANG_AVAIABLE
#include <x86intrin.h>
#endif

#include <immintrin.h>

#include <type_traits>

#include <boost/integer.hpp>

namespace vefs::crypto
{
    namespace
    {
        template <typename T, typename = void>
        struct has_adc_u64 : std::false_type
        {
        };
        template <typename T>
        struct has_adc_u64<T, std::void_t<decltype(_addcarry_u64(0, T{}, T{}, nullptr))>>
            : std::true_type
        {
        };
        constexpr bool has_adc_u64_v = has_adc_u64<unsigned long long>::value;

        template <typename T, typename = void>
        struct has_adc_u32 : std::false_type
        {
        };
        template <typename T>
        struct has_adc_u32<T, std::void_t<decltype(_addcarry_u32(0, T{}, T{}, nullptr))>>
            : std::true_type
        {
        };
        constexpr bool has_adc_u32_v = has_adc_u32<unsigned int>::value;

        template <typename T = std::size_t>
        inline unsigned char add_carry(unsigned char carry, T a, T b, T *out)
        {
            static_assert(std::is_integral_v<T>);
            static_assert(std::is_unsigned_v<T>);

            [[maybe_unused]] const auto upcast_impl = [&]([[maybe_unused]] auto acc) {
                acc += a;
                acc += b;
                *out = static_cast<T>(acc);
                return static_cast<unsigned char>(acc >> std::numeric_limits<T>::digits);
            };

            if constexpr (sizeof(T) < sizeof(std::uint_fast32_t))
            {
                return upcast_impl(static_cast<std::uint_fast32_t>(carry));
            }
            else if constexpr (sizeof(T) == 4 && has_adc_u32_v)
            {
                return _addcarry_u32(carry, a, b, reinterpret_cast<std::uint32_t *>(out));
            }
            else if constexpr (sizeof(T) < 8)
            {
                return upcast_impl(static_cast<std::uint_fast64_t>(carry));
            }
            else if constexpr (sizeof(T) == 8 && has_adc_u64_v)
            {
                return _addcarry_u64(carry, a, b, reinterpret_cast<unsigned long long *>(out));
            }
            else
            {
                using fraction_t =
                    boost::uint_t<std::numeric_limits<std::size_t>::digits / 2>::exact;
                constexpr auto limit = sizeof(T) / sizeof(fraction_t);

                auto fa = reinterpret_cast<fraction_t *>(&a);
                auto fb = reinterpret_cast<fraction_t *>(&b);
                auto fout = reinterpret_cast<fraction_t *>(out);
                std::size_t state = carry;

                for (auto i = 0; i < limit; ++i)
                {
                    state += static_cast<std::size_t>(fa[i]) + fb[i];
                    fout[i] = static_cast<fraction_t>(state);
                    state >>= std::numeric_limits<fraction_t>::digits;
                }

                static_assert(sizeof(T) % sizeof(fraction_t) == 0);
                /*
                if constexpr (sizeof(T) % sizeof(fraction_t) != 0)
                {
                    // I don't think there is any use case for this...
                }
                */

                return static_cast<unsigned char>(state);
            }
        }

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma inline_recursion(on)
#endif

        template <std::size_t size>
        inline void increment_impl(std::size_t *state, unsigned char carry)
        {
            static_assert(size % sizeof(std::size_t) == 0);
            constexpr auto remaining = size - sizeof(std::size_t);

            if constexpr (remaining)
            {
                auto next_carry = add_carry<std::size_t>(carry, *state, 0, state);
                increment_impl<remaining>(++state, next_carry);
            }
            else
            {
                (*state) += carry;
            }
        }
    } // namespace

    void counter::increment()
    {
        auto p = mCtrState.data();
        auto first_carry = add_carry<std::size_t>(0, *p, 1, p);
        increment_impl<state_size - sizeof(*p)>(p + 1, first_carry);
    }
} // namespace vefs::crypto
