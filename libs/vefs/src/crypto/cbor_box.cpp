#include "cbor_box.hpp"

#include <dplx/dp/decoder/tuple_utils.hpp>
#include <dplx/dp/item_emitter.hpp>
#include <dplx/dp/item_parser.hpp>
#include <dplx/dp/streams/memory_input_stream.hpp>
#include <dplx/dp/streams/memory_output_stream.hpp>

using namespace dplx;

namespace vefs::crypto
{

auto cbor_box_layout_head(dplx::dp::memory_buffer &outStream,
                          std::uint16_t dataLength) noexcept
        -> result<cbor_box_layout>
{
    using namespace dplx;

    using emit = dp::item_emitter<dp::memory_buffer>;

    VEFS_TRY(emit::array(outStream, 3U));

    VEFS_TRY(emit::binary(outStream, box_salt_size));
    rw_blob<32> salt(outStream.consume(box_salt_size), box_salt_size);

    VEFS_TRY(emit::binary(outStream, box_mac_size));
    rw_blob<16> mac(outStream.consume(box_mac_size), box_mac_size);

    VEFS_TRY(emit::binary(outStream, dataLength));

    return cbor_box_layout{salt, mac};
}

auto cbor_box_decode_head(dplx::dp::memory_buffer &inStream) noexcept
        -> result<cbor_box_head>
{
    using namespace dplx;
    using parse = dp::item_parser<dp::memory_buffer>;

    VEFS_TRY(auto &&head, dp::parse_tuple_head(inStream));

    if (head.num_properties != 3)
    {
        return dp::errc::tuple_size_mismatch;
    }
    if (inStream.remaining_size() < (2 + 32 + 1 + 16))
    {
        return dp::errc::end_of_stream;
    }

    // 32B salt
    VEFS_TRY(parse::expect(inStream, dp::type_code::binary, 32U,
                           dp::parse_mode::strict));
    ro_blob<32> salt(inStream.consume(32), 32);

    // 16B mac
    VEFS_TRY(parse::expect(inStream, dp::type_code::binary, 16U,
                           dp::parse_mode::strict));
    ro_blob<16> mac(inStream.consume(16u), 16);

    VEFS_TRY(auto info, parse::generic(inStream));
    if (info.type != dp::type_code::binary && !info.indefinite())
    {
        return dp::errc::item_type_mismatch;
    }
    if (!std::in_range<int>(info.value))
    {
        return dp::errc::item_value_out_of_range;
    }
    auto dataLength = static_cast<int>(info.value);
    if (inStream.remaining_size() < info.value)
    {
        return dp::errc::end_of_stream;
    }

    return cbor_box_head{salt, mac, dataLength};
}

} // namespace vefs::crypto
