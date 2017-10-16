#include "precompiled.hpp"
#include <vefs/crypto/counter.hpp>

#include <intrin.h>

#include <type_traits>

#include <boost/predef/architecture.h>
#include <boost/integer.hpp>

namespace vefs::crypto
{
    namespace
    {
        template <typename T>
        size_t loadxx(blob src, T &out)
        {
            static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);

            const auto limit = src.size();
            if (sizeof(T) <= limit)
            {
                out = *reinterpret_cast<const T *>(src.data());
                return sizeof(T);
            }
            else
            {
                out = T{};
                for (auto i = 0; i < limit; ++i)
                {
                    out |= std::to_integer<T>(src[i]) << (i << 3);
                }
                return limit;
            }
        }
        template <typename T>
        void storexx(blob loc, T value)
        {
            static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);

            const auto limit = loc.size();
            if (sizeof(T) <= limit)
            {
                *reinterpret_cast<T *>(loc.data()) = value;
            }
            else
            {
                for (auto i = 0; i < limit; ++i)
                {
                    loc[i] = std::byte{ (value >> (i << 3)) & 0xFF };
                }
            }
        }

        template <size_t StepSize>
        void increment_big_num_impl(blob state)
        {
            if constexpr (StepSize == 8 || StepSize == 4)
            {
                using uint_t = typename boost::uint_t<StepSize * 8>::exact;

                unsigned char carry = 1;
                uint_t in = 0;
                uint_t out = 0;
                size_t offset;
                do
                {
                    offset = loadxx(state, in);

                    if constexpr (StepSize == 8)
                    {
                        carry = _addcarry_u64(carry, in, 0, &out);
                    }
                    else
                    {
                        carry = _addcarry_u32(carry, in, 0, &out);
                    }

                    storexx(state, out);
                    state.remove_prefix(offset);
                } while (state);
            }
            else
            {
                uint_fast16_t carry = 1;
                const auto limit = state.size();
                for (auto i = 0; i < limit; i++) {
                    carry += std::to_integer<uint_fast16_t>(state[i]);
                    state[i] = std::byte{ carry };
                    carry >>= 8;
                }
            }
        }

        void increment_big_num(blob state)
        {
#if defined BOOST_ARCH_X86_64_AVAILABLE
            constexpr size_t stepSize = 8;
#elif defined BOOST_ARCH_X86_AVAILABLE
            constexpr size_t stepSize = 4;
#else
            constexpr size_t stepSize = 1;
#endif
            increment_big_num_impl<stepSize>(state);
        }
    }

    void counter::increment()
    {
        guard_t sync{ mAccessMutex };
        increment_big_num(blob{ mCtrState });
    }

    counter::state counter::fetch_increment()
    {
        guard_t sync{ mAccessMutex };
        auto ctrState = mCtrState;
        increment_big_num(blob{ mCtrState });
        return ctrState;
    }
}
