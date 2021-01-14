#include "cbor_box.hpp"

#include <dplx/dp/item_emitter.hpp>
#include <dplx/dp/item_parser.hpp>
#include <dplx/dp/streams/memory_input_stream.hpp>
#include <dplx/dp/streams/memory_output_stream.hpp>

#include "../detail/cbor_utils.hpp"

using namespace dplx;

namespace vefs::crypto
{

    auto cbor_box_layout_head(dplx::dp::byte_buffer_view &outStream,
                              std::uint16_t dataLength) noexcept
        -> result<cbor_box_layout>
    {
        using namespace dplx;

        using emit = dp::item_emitter<dp::byte_buffer_view>;

        DPLX_TRY(emit::array(outStream, 3u));

        DPLX_TRY(emit::binary(outStream, 32u));
        rw_blob<32> salt(outStream.consume(32), 32);

        DPLX_TRY(emit::binary(outStream, 16u));
        rw_blob<16> mac(outStream.consume(16), 16);

        DPLX_TRY(emit::binary(outStream, dataLength));

        return cbor_box_layout{salt, mac};
    }

    auto cbor_box_decode_head(dplx::dp::byte_buffer_view &inStream) noexcept
        -> result<cbor_box_head>
    {
        using namespace dplx;

        DPLX_TRY(auto &&head, dp::parse_tuple_head(inStream));

        if (head.num_properties != 3)
        {
            return dp::errc::tuple_size_mismatch;
        }
        if (inStream.remaining_size() < (2 + 32 + 1 + 16))
        {
            return dp::errc::end_of_stream;
        }

        // 32B salt
        DPLX_TRY(auto info, dp::detail::parse_item_info(inStream));
        if (std::byte{info.type} != dp::type_code::binary)
        {
            return dp::errc::item_type_mismatch;
        }
        if (info.value != 32u)
        {
            return dp::errc::item_value_out_of_range;
        }
        if (info.encoded_length != 2)
        {
            return dp::errc::invalid_additional_information;
        }
        ro_blob<32> salt(inStream.consume(32), 32);

        // 16B mac
        DPLX_TRY(info, dp::detail::parse_item_info(inStream));
        if (std::byte{info.type} != dp::type_code::binary)
        {
            return dp::errc::item_type_mismatch;
        }
        if (info.value != 16u)
        {
            return dp::errc::item_value_out_of_range;
        }
        if (info.encoded_length != 1)
        {
            return dp::errc::invalid_additional_information;
        }
        ro_blob<16> mac(inStream.consume(16u), 16);

        DPLX_TRY(info, dp::detail::parse_item_info(inStream));
        if (std::byte{info.type} != dp::type_code::binary)
        {
            return dp::errc::item_type_mismatch;
        }
        if (info.value >
            static_cast<unsigned int>(std::numeric_limits<int>::max()))
        {
            return dp::errc::item_value_out_of_range;
        }
        auto dataLength = static_cast<int>(info.value);
        if (inStream.remaining_size() < dataLength)
        {
            return dp::errc::end_of_stream;
        }

        return cbor_box_head{salt, mac, dataLength};
    }

} // namespace vefs::crypto
