#pragma once

#include <cstddef>
#include <type_traits>

#include <vefs/blob.hpp>

namespace vefs::utils
{
    void secure_memzero(blob data);

    template <typename T, std::enable_if_t<std::is_trivially_destructible_v<T>, int> = 0>
    void secure_data_erase(T &data)
    {
        secure_memzero(blob{ reinterpret_cast<std::byte *>(&data), sizeof(T) });
    }
}
