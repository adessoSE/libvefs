#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <array>
#include <limits>
#include <tuple>
#include <type_traits>

#include <vefs/span.hpp>

namespace vefs::utils
{
namespace bitset_ops
{
template <typename Unit>
constexpr std::tuple<std::size_t, Unit> offset_and_mask_of(std::size_t bitpos)
{
    static_assert(std::is_unsigned_v<Unit>);

    return {bitpos / std::numeric_limits<Unit>::digits,
            static_cast<Unit>(static_cast<Unit>(1)
                              << (bitpos % std::numeric_limits<Unit>::digits))};
}

template <typename Unit>
inline void set(Unit *begin, std::size_t bitpos)
{
    auto [offset, mask] = offset_and_mask_of<Unit>(bitpos);

    begin[offset] |= mask;
}

template <typename Unit>
inline void set(Unit *begin, std::size_t bitpos, bool value)
{
    auto [offset, mask] = offset_and_mask_of<Unit>(bitpos);

    auto bits = -static_cast<std::make_signed_t<Unit>>(value);
    begin[offset] ^= (static_cast<Unit>(bits) ^ begin[offset]) & mask;
}

template <typename Unit>
inline void unset(Unit *begin, std::size_t bitpos)
{
    auto [offset, mask] = offset_and_mask_of<Unit>(bitpos);

    begin[offset] &= ~mask;
}

template <typename Unit>
inline void flip(Unit *begin, std::size_t bitpos)
{
    auto [offset, mask] = offset_and_mask_of<Unit>(bitpos);

    begin[offset] ^= mask;
}

template <typename Unit>
inline bool get(Unit const *begin, std::size_t bitpos)
{
    auto [offset, mask] = offset_and_mask_of<Unit>(bitpos);

    return begin[offset] & mask;
}

template <typename Unit>
inline void set_n(Unit *begin, std::size_t const numBits)
{
    using byte_limits = std::numeric_limits<std::uint8_t>;

    auto const dist = numBits / byte_limits::digits;
    auto *const pend = reinterpret_cast<std::uint8_t *>(begin) + dist;

    std::memset(begin, byte_limits::max(), dist);

    if (auto const remaining = numBits % byte_limits::digits)
    {
        constexpr std::array<std::uint8_t, 8> lut
                = {0b00000000, 0b00000001, 0b00000011, 0b00000111,
                   0b00001111, 0b00011111, 0b00111111, 0b01111111};
        *pend |= lut[remaining];
    }
}
} // namespace bitset_ops

class const_bitset_overlay;

class bitset_overlay
{
    friend class const_bitset_overlay;

public:
    using unit_type = unsigned char;

    bitset_overlay(rw_dynblob data)
        : mBegin(reinterpret_cast<unit_type *>(data.data()))
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
        return reference{*this, bitpos};
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
    using unit_type = unsigned char;

    const_bitset_overlay(ro_dynblob data)
        : mBegin(reinterpret_cast<unit_type const *>(data.data()))
    {
        assert(data.size() >= sizeof(unit_type));
        assert(data.size() % sizeof(unit_type) == 0);
    }
    const_bitset_overlay(bitset_overlay other)
        : mBegin{other.mBegin}
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

    auto data() const noexcept -> unit_type const *
    {
        return mBegin;
    }

private:
    unit_type const *mBegin;
};
} // namespace vefs::utils
