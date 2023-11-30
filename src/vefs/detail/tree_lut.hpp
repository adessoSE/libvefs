#pragma once

#include <cstddef>
#include <cstdint>

#include <array>
#include <limits>

#include <vefs/utils/misc.hpp>

#include "sector_device.hpp"

namespace vefs::detail::lut
{
// reference count per sector, one reference has 32 byte
constexpr auto references_per_sector = sector_device::sector_payload_size / 32;
// payload_size * references_per_sector^4 < 2^64 < payload_size *
// references_per_sector^5
constexpr int max_tree_depth = 4;
} // namespace vefs::detail::lut

namespace vefs::detail::lut::detail
{
/**
 * calculates a Lookup-Table with the step width for each tree level
 * index 0 is tree depth -1 and 1 is tree depth 0
 * @return step width in bit
 */
constexpr auto compute_step_width_lut()
{
    std::array<std::uint64_t, max_tree_depth + 2> lut{};
    lut[0] = 1;
    lut[1] = sector_device::sector_payload_size;
    for (std::size_t i = 2; i < lut.size(); ++i)
    {
        lut[i] = lut[i - 1] * references_per_sector;
    }
    return lut;
}

/**
 * calculates a Lookup-Table with the count of sectors that fit in one tree
 * level
 */
constexpr auto compute_ref_width_lut()
{
    // zero is tree depth -1
    // one is tree depth 0
    std::array<std::uint64_t, max_tree_depth + 1> lut{};
    lut[0] = 1;
    for (std::size_t i = 1; i < lut.size(); ++i)
    {
        lut[i] = lut[i - 1] * references_per_sector;
    }
    return lut;
}
} // namespace vefs::detail::lut::detail

namespace vefs::detail::lut
{
constexpr auto step_width{detail::compute_step_width_lut()};
constexpr auto ref_width{detail::compute_ref_width_lut()};

/**
 * calculates the tree level for a given sector position
 * @param sectorPos the sector position for that the tree level is calculated
 * @return  the tree level starting with 0
 */
constexpr auto required_tree_depth(std::uint64_t sectorPos) -> int
{
    static_assert(ref_width.size() == 5); // safe guard for ref_width changes.
    return 0 + static_cast<int>(sectorPos >= ref_width[0])
           + static_cast<int>(sectorPos >= ref_width[1])
           + static_cast<int>(sectorPos >= ref_width[2])
           + static_cast<int>(sectorPos >= ref_width[3])
           + static_cast<int>(sectorPos >= ref_width[4]);
}

/**
 * calculates the sector position for a given byte position
 * @param bytePos the byte position for that the sector is returned
 * @return the sector position where this byte is in
 */
constexpr auto sector_position_of(std::uint64_t bytePos) -> std::uint64_t
{
    return bytePos / sector_device::sector_payload_size;
}

/**
 * calculates the total amount of sectors occupied by a given file size, i.e.
 * including the reference sector overhead \param byteSize the file size in
 * bytes
 */
constexpr auto required_sector_count(const std::uint64_t byteSize)
        -> std::uint64_t
{
    static_assert(ref_width.size() == 5); // safe guard for ref_width changes.

    auto numSectors
            = byteSize != 0u ? utils::div_ceil(byteSize, step_width[1]) : 1;
    if (byteSize > step_width[1])
    {
        numSectors += utils::div_ceil(byteSize, step_width[2]);
        if (byteSize > step_width[2])
        {
            numSectors += utils::div_ceil(byteSize, step_width[3]);
            if (byteSize > step_width[3])
            {
                numSectors += utils::div_ceil(byteSize, step_width[4]);
                if (byteSize > step_width[4])
                {
                    numSectors += utils::div_ceil(byteSize, step_width[5]);
                    if (byteSize > step_width[5])
                    {
                        numSectors += 1;
                    }
                }
            }
        }
    }
    return numSectors;
}
} // namespace vefs::detail::lut
