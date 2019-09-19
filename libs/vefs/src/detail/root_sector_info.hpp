#pragma once

#include <cstddef>
#include <cstdint>

#include <array>

#include "sector_id.hpp"

namespace vefs::detail
{
    struct sector_reference
    {
        sector_id sector;
        std::array<std::byte, 16> mac;
    };

    class root_sector_info
    {
    public:
        sector_reference root;
        std::uint64_t maximum_extent;
        int tree_depth;
    };
} // namespace vefs::detail
