#pragma once

#include <ostream>

#include <fmt/format.h>

#include <vefs/disappointment.hpp>
#include <vefs/utils/random.hpp>

struct test_rng : vefs::utils::xoroshiro128plus
{
    // default initialize the test rng to the first 32 hex digits of pi
    // pi is random enough to be a good seed and hard coding it here
    // guarantees that the test cases are reproducible
    test_rng()
        : xoroshiro128plus(0x243F'6A88'85A3'08D3ull, 0x1319'8A2E'0370'7344ull)
    {
    }
    using xoroshiro128plus::xoroshiro128plus;
};

namespace vefs
{
    inline std::ostream & operator<<(std::ostream &s, const error_info &info)
    {
        using namespace fmt::literals;
        s << "{:v}"_format(info);
    }

}


