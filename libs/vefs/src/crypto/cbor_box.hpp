#pragma once

#include <dplx/dp/byte_buffer.hpp>

#include <vefs/disappointment.hpp>
#include <vefs/span.hpp>

#include "provider.hpp"

namespace vefs::crypto
{
struct cbor_box_head
{
    ro_blob<32> salt;
    ro_blob<16> mac;
    int dataLength;
};
struct cbor_box_layout
{
    rw_blob<32> salt;
    rw_blob<16> mac;
};

auto cbor_box_layout_head(dplx::dp::byte_buffer_view &outStream,
                          std::uint16_t dataLength) noexcept
        -> result<cbor_box_layout>;

auto cbor_box_decode_head(dplx::dp::byte_buffer_view &inStream) noexcept
        -> result<cbor_box_head>;
} // namespace vefs::crypto
