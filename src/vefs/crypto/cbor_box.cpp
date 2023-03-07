#include "cbor_box.hpp"

#include <dplx/dp.hpp>
#include <dplx/dp/api.hpp>
#include <dplx/dp/codecs/auto_tuple.hpp>
#include <dplx/dp/items/emit_core.hpp>
#include <dplx/dp/items/parse_core.hpp>
#include <dplx/dp/legacy/memory_buffer.hpp>
#include <dplx/dp/legacy/memory_input_stream.hpp>
#include <dplx/dp/legacy/memory_output_stream.hpp>

using namespace dplx;

namespace vefs::crypto
{

auto cbor_box_layout_head(dplx::dp::memory_buffer &outStream,
                          std::uint16_t dataLength) noexcept
        -> dplx::dp::result<cbor_box_layout>
{
    using namespace dplx;
    auto &&outBuffer = dp::get_output_buffer(outStream);
    dp::emit_context ctx{outBuffer};

    DPLX_TRY(dp::emit_array(ctx, 3U));

    DPLX_TRY(dp::emit_binary(ctx, box_salt_size));
    rw_blob<box_salt_size> salt(outBuffer.data(), box_salt_size);
    outBuffer.commit_written(box_salt_size);

    DPLX_TRY(dp::emit_binary(ctx, box_mac_size));
    rw_blob<box_mac_size> mac(outBuffer.data(), box_mac_size);
    outBuffer.commit_written(box_mac_size);

    DPLX_TRY(dp::emit_binary(ctx, dataLength));

    DPLX_TRY(outBuffer.sync_output());
    return cbor_box_layout{salt, mac};
}

auto cbor_box_decode_head(dplx::dp::memory_buffer &inStream) noexcept
        -> dplx::dp::result<cbor_box_head>
{
    using namespace dplx;

    auto &&inBuffer = dp::get_input_buffer(inStream);
    dp::parse_context ctx{inBuffer};

    DPLX_TRY(auto &&head, dp::decode_tuple_head(ctx));

    if (head.num_properties != 3)
    {
        return dp::errc::tuple_size_mismatch;
    }
    if (inBuffer.size() < (2 + box_salt_size + 1 + box_mac_size))
    {
        return dp::errc::end_of_stream;
    }

    // 32B salt
    DPLX_TRY(dp::expect_item_head(ctx, dp::type_code::binary, box_salt_size));
    ro_blob<32> salt(inBuffer.data(), box_salt_size);
    inBuffer.discard_buffered(box_salt_size);

    // 16B mac
    DPLX_TRY(dp::expect_item_head(ctx, dp::type_code::binary, box_mac_size));
    ro_blob<16> mac(inBuffer.data(), box_mac_size);
    inBuffer.discard_buffered(box_mac_size);

    DPLX_TRY(auto info, dp::parse_item_head(ctx));
    if (info.type != dp::type_code::binary && !info.indefinite())
    {
        return dp::errc::item_type_mismatch;
    }
    if (!std::in_range<int>(info.value))
    {
        return dp::errc::item_value_out_of_range;
    }
    auto dataLength = static_cast<int>(info.value);
    if (inBuffer.size() < info.value)
    {
        return dp::errc::end_of_stream;
    }

    DPLX_TRY(inBuffer.sync_input());
    return cbor_box_head{salt, mac, dataLength};
}

} // namespace vefs::crypto
