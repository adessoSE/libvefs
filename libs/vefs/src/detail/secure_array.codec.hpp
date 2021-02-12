#pragma once

#include <cstddef>

#include <span>

#include <dplx/dp/decoder/std_container.hpp>
#include <dplx/dp/encoder/core.hpp>

#include <vefs/utils/secure_array.hpp>

namespace dplx::dp
{
template <std::size_t N, input_stream Stream>
class basic_decoder<vefs::utils::secure_byte_array<N>, Stream>
    : public basic_decoder<std::span<std::byte>, Stream>
{
};
} // namespace dplx::dp
