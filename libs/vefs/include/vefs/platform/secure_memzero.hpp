#pragma once

#include <memory>
#include <type_traits>

#include <vefs/span.hpp>

namespace vefs::utils
{
void secure_memzero(rw_dynblob data);

template <typename T>
void secure_data_erase(T &data)
{
    if constexpr (!std::is_trivially_destructible_v<T>)
    {
        std::destroy_at(&data);
    }
    rw_dynblob storage = rw_blob_cast(data);
    secure_memzero(storage);
}
} // namespace vefs::utils
