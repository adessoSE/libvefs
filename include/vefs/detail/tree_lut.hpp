#pragma once

#include <cstddef>
#include <cstdint>

#include <array>
#include <limits>

#include <vefs/utils/misc.hpp>
#include <vefs/detail/raw_archive.hpp>


namespace vefs::detail::lut
{
    constexpr auto references_per_sector = raw_archive::sector_payload_size / 32;
    constexpr int max_tree_depth = 4; // payload_size * references_per_sector^4 < 2^64 < payload_size * references_per_sector^4
}

namespace vefs::detail::lut::detail
{
    constexpr auto compute_step_width_lut()
    {
        // zero is tree depth -1
        // one is tree depth 0
        std::array<std::uint64_t, max_tree_depth + 2> lut{};
        lut[0] = 1;
        lut[1] = raw_archive::sector_payload_size;
        for (auto i = 2; i < lut.size(); ++i)
        {
            lut[i] = lut[i - 1] * references_per_sector;
        }
        return lut;
    }

    constexpr auto compute_ref_width_lut()
    {
        // zero is tree depth -1
        // one is tree depth 0
        std::array<std::uint64_t, max_tree_depth + 1> lut{};
        lut[0] = 1;
        for (auto i = 1; i < lut.size(); ++i)
        {
            lut[i] = lut[i - 1] * references_per_sector;
        }
        return lut;
    }
}

namespace vefs::detail::lut
{
    constexpr auto step_width{ detail::compute_step_width_lut() };
    constexpr auto ref_width{ detail::compute_ref_width_lut() };
}
