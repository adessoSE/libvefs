#pragma once

#include <cstddef>
#include <cstdint>

#include <array>

#include "sector_id.hpp"

namespace vefs::detail
{
    class root_sector_info
    {
    public:
        std::array<std::byte, 16> start_block_mac;
        sector_id root_block_idx;
        std::uint64_t file_size;
        int tree_depth;
    };
}
