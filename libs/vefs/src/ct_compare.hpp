#pragma once

#include <cstddef>
#include <cstdint>

#include <limits>
#include <type_traits>

#include <vefs/blob.hpp>
#include <vefs/disappointment.hpp>

namespace vefs::crypto::detail
{
    // compares two little endian big nums in constant time
    inline result<int> ct_compare(blob_view l, blob_view r) noexcept
    {
        if (l.size() != r.size() || !l || !r)
        {
            return errc::invalid_argument;
        }

        unsigned int gt = 0;
        unsigned int eq = 1;
        for (std::size_t i = l.size(); i != 0; )
        {
            --i;
            auto lp = std::to_integer<unsigned int>(l[i]);
            auto rp = std::to_integer<unsigned int>(r[i]);

            constexpr auto signShift = (std::numeric_limits<unsigned int>::digits - 1);
            gt |= ((rp - lp) >> signShift) & eq;
            eq &= 1 ^ (((lp - rp) | (rp - lp)) >> signShift);
        }

        return static_cast<int>((gt << 1) | eq) - 1;
    }
}
