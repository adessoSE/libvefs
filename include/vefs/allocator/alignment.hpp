#pragma once

#include <cstddef>

#include <algorithm>

#include <vefs/utils/misc.hpp>

namespace vefs::detail
{
constexpr auto realigning_elem_size(std::size_t elemSize,
                                    std::size_t alignment) noexcept
        -> std::size_t
{
    return utils::div_ceil(std::max(elemSize, std::size_t{1}), alignment)
         * alignment;
}

template <std::size_t ELEM_SIZE, std::size_t ALIGNMENT>
struct alignmnent_values
{
    static constexpr std::size_t alignment = ALIGNMENT;
    static constexpr std::size_t elem_size = ELEM_SIZE;

    static constexpr std::size_t adj_elem_size
            = realigning_elem_size(elem_size, alignment);
    static constexpr std::size_t adj_elem_overhead = adj_elem_size - elem_size;
};
} // namespace vefs::detail
