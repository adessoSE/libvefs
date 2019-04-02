#pragma once

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <cstring>

#include <array>
#include <tuple>
#include <limits>
#include <type_traits>

#include <vefs/blob.hpp>

namespace vefs::utils
{
    namespace bitset_ops
    {
        template <typename Unit>
        inline std::tuple<std::size_t, Unit> offset_and_mask_of(std::size_t bitpos)
        {
            static_assert(std::is_unsigned_v<Unit>);

            return {
                bitpos / std::numeric_limits<Unit>::digits,
                static_cast<Unit>(1) << (bitpos % std::numeric_limits<Unit>::digits)
            };
        }

        template <typename Unit>
        inline void set(Unit *begin, std::size_t bitpos)
        {
            auto[offset, mask] = offset_and_mask_of<Unit>(bitpos);

            begin[offset] |= mask;
        }

        template <typename Unit>
        inline void set(Unit *begin, std::size_t bitpos, bool value)
        {
            auto[offset, mask] = offset_and_mask_of<Unit>(bitpos);

            auto bits = -static_cast<std::make_signed_t<Unit>>(value);
            begin[offset] ^= (static_cast<Unit>(bits) ^ begin[offset]) & mask;
        }

        template <typename Unit>
        inline void unset(Unit *begin, std::size_t bitpos)
        {
            auto[offset, mask] = offset_and_mask_of<Unit>(bitpos);

            begin[offset] &= ~mask;
        }

        template <typename Unit>
        inline void flip(Unit *begin, std::size_t bitpos)
        {
            auto[offset, mask] = offset_and_mask_of<Unit>(bitpos);

            begin[offset] ^= mask;
        }

        template <typename Unit>
        inline bool get(const Unit *begin, std::size_t bitpos)
        {
            auto[offset, mask] = offset_and_mask_of<Unit>(bitpos);

            return begin[offset] & mask;
        }

        template <typename Unit>
        inline void set_n(Unit *begin, const std::size_t numBits)
        {
            using limits = std::numeric_limits<Unit>;
            using byte_limits = std::numeric_limits<std::uint8_t>;

            const auto dist = numBits / byte_limits::digits;
            auto *const pend = reinterpret_cast<std::uint8_t *>(begin) + dist;

            std::memset(begin, byte_limits::max(), dist);

            if (const auto remaining = numBits % byte_limits::digits)
            {
                constexpr std::array<std::uint8_t, 8> lut = {
                    0b0000'0000, 0b0000'0001, 0b0000'0011, 0b0000'0111,
                    0b0000'1111, 0b0001'1111, 0b0011'1111, 0b0111'1111
                };
                *pend |= lut[remaining];
            }
        }
    }

    class const_bitset_overlay;

    class bitset_overlay
    {
        friend class const_bitset_overlay;

    public:
        using unit_type = std::size_t;

        bitset_overlay(rw_dynblob data)
            : mBegin(reinterpret_cast<unit_type *>(data.data())) // #UB-Alignment
        {
            assert(data.size() >= sizeof(unit_type));
            assert(data.size() % sizeof(unit_type) == 0);
        }

        void set(std::size_t bitpos)
        {
            bitset_ops::set(mBegin, bitpos);
        }
        void set(std::size_t bitpos, bool value)
        {
            bitset_ops::set(mBegin, bitpos, value);
        }

        void set_n(std::size_t num)
        {
            bitset_ops::set_n(mBegin, num);
        }

        void unset(std::size_t bitpos)
        {
            bitset_ops::unset(mBegin, bitpos);
        }

        void flip(std::size_t bitpos)
        {
            bitset_ops::flip(mBegin, bitpos);
        }

        bool get(std::size_t bitpos) const
        {
            return bitset_ops::get(mBegin, bitpos);
        }

        class reference
        {
            friend class bitset_overlay;

            reference(bitset_overlay &owner, std::size_t bitpos)
                : mOwner(owner)
                , mBitpos(bitpos)
            {
            }

        public:
            void set()
            {
                mOwner.set(mBitpos);
            }
            void unset()
            {
                mOwner.unset(mBitpos);
            }
            void flip()
            {
                mOwner.flip(mBitpos);
            }

            operator bool() const
            {
                return mOwner.get(mBitpos);
            }

            bool operator=(bool value)
            {
                mOwner.set(mBitpos, value);
                return value;
            }
            bool operator~() const
            {
                return !mOwner.get(mBitpos);
            }

        private:
            bitset_overlay &mOwner;
            std::size_t mBitpos;
        };

        reference operator[](std::size_t bitpos)
        {
            return reference{ *this, bitpos };
        }
        bool operator[](std::size_t bitpos) const
        {
            return get(bitpos);
        }

    private:
        unit_type *mBegin;
    };

    class const_bitset_overlay
    {
    public:
        using unit_type = std::size_t;

        const_bitset_overlay(ro_dynblob data)
            : mBegin(reinterpret_cast<const unit_type *>(data.data())) // #UB-Alignment
        {
            assert(data.size() >= sizeof(unit_type));
            assert(data.size() % sizeof(unit_type) == 0);
        }
        const_bitset_overlay(bitset_overlay other)
            : mBegin{ other.mBegin }
        {
        }

        bool get(std::size_t bitpos) const
        {
            return bitset_ops::get(mBegin, bitpos);
        }

        bool operator[](std::size_t bitpos) const
        {
            return get(bitpos);
        }

    private:
        const unit_type *mBegin;
    };
}
