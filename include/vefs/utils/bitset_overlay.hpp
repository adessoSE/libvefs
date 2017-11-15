#pragma once

#include <cstddef>
#include <cassert>

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
    }

    class bitset_overlay
    {
    public:
        using unit_type = std::size_t;

        bitset_overlay(blob data)
            : mBegin(&data.as<unit_type>())
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

    private:
        unit_type *mBegin;
    };
}
