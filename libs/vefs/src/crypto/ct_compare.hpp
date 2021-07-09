#pragma once

#include <cstddef>
#include <cstdint>

#include <limits>
#include <type_traits>

#include <vefs/disappointment.hpp>
#include <vefs/span.hpp>

namespace vefs::crypto::detail
{
// compares two little endian big nums in constant time
inline result<int> ct_compare(ro_dynblob l, ro_dynblob r) noexcept
{
    if (l.size() != r.size() || !l || !r)
    {
        return errc::invalid_argument;
    }

    unsigned int gt = 0;
    unsigned int eq = 1;
    for (std::size_t i = l.size(); i != 0;)
    {
        // #todo is this that the compiler does not optimize it?
        --i;
        auto lp = std::to_integer<unsigned int>(l[i]);
        auto rp = std::to_integer<unsigned int>(r[i]);

        constexpr auto signShift
                = std::numeric_limits<unsigned int>::digits - 1;
        // gt: rp > lp
        gt |= ((rp - lp) >> signShift) & eq;
        // eq: rp == lp
        eq &= 1 ^ (((lp - rp) | (rp - lp)) >> signShift);
    }
    // r>l:(gt=1,eq=0)->1; r==l:(gt=0,eq=1)->0; (gt=0, eq=1)->-1;
    return static_cast<int>((gt << 1) | eq) - 1;
}
} // namespace vefs::crypto::detail
