#pragma once

#include <array>

#include "file_descriptor.hpp"

namespace vefs::detail
{
struct archive_header
{
    file_descriptor filesystem_index;
    file_descriptor free_sector_index;

    std::array<std::byte, 16> archive_secret_counter;
    std::array<std::byte, 16> journal_counter;
};
} // namespace vefs::detail
