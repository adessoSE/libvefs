#pragma once

#include <vefs/utils/bit.hpp>

namespace vefs::utils
{
    template <typename T>
    inline bool bit_scan(std::size_t &pos, T data)
    {
        if (data == 0)
        {
            return false;
        }
        pos = countr_zero(data);
        return true;
    }
} // namespace vefs::utils
