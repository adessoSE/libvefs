#pragma once

#include <dplx/dp/disappointment.hpp>
#include <dplx/dp/memory_buffer.hpp>

#include <vefs/crypto/provider.hpp>
#include <vefs/disappointment.hpp>
#include <vefs/span.hpp>

namespace vefs::crypto
{
inline constexpr unsigned box_salt_size = 32U;
inline constexpr unsigned box_mac_size = 16U;

struct cbor_box_head
{
    ro_blob<box_salt_size> salt;
    ro_blob<box_mac_size> mac;
    int dataLength;
};
struct cbor_box_layout
{
    rw_blob<box_salt_size> salt;
    rw_blob<box_mac_size> mac;
};

auto cbor_box_layout_head(dplx::dp::memory_buffer &outStream,
                          std::uint16_t dataLength) noexcept
        -> dplx::dp::result<cbor_box_layout>;

auto cbor_box_decode_head(dplx::dp::memory_buffer &inStream) noexcept
        -> dplx::dp::result<cbor_box_head>;
} // namespace vefs::crypto
