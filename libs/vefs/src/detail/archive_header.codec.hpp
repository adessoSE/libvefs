#pragma once

#include <dplx/dp/decoder/api.hpp>
#include <dplx/dp/decoder/core.hpp>
#include <dplx/dp/decoder/std_container.hpp>
#include <dplx/dp/decoder/std_string.hpp>
#include <dplx/dp/encoder/api.hpp>
#include <dplx/dp/encoder/core.hpp>

#include "file_descriptor.codec.hpp"
#include "secure_array.codec.hpp"

#include "archive_header.hpp"

namespace dplx::dp
{
template <input_stream Stream>
class basic_decoder<vefs::detail::archive_header, Stream>
{
public:
    using value_type = vefs::detail::archive_header;
    inline auto operator()(Stream &inStream, value_type &value) const
            -> result<void>
    {
        DPLX_TRY(auto &&headInfo,
                 dp::parse_object_head(inStream, std::true_type{}));

        if (headInfo.num_properties != 4 || headInfo.version != 0)
        {
            return errc::item_version_mismatch;
        }

        unsigned int parsed = 0;
        for (auto i = 0; i < 4; ++i)
        {
            DPLX_TRY(auto &&info, detail::parse_item_info(inStream));
            if (static_cast<std::byte>(info.type) != type_code::posint)
            {
                return errc::unknown_property;
            }

            switch (info.value)
            {
            case 1:
            {
                if (parsed & 0x01)
                {
                    return errc::required_object_property_missing;
                }
                parsed |= 0x01;
                DPLX_TRY(decode(inStream, value.filesystem_index));
            }
            break;

            case 2:
            {
                if (parsed & 0x02)
                {
                    return errc::required_object_property_missing;
                }
                parsed |= 0x02;
                DPLX_TRY(decode(inStream, value.free_sector_index));
            }
            break;

            case 3:
            {
                if (parsed & 0x04)
                {
                    return errc::required_object_property_missing;
                }
                parsed |= 0x04;
                DPLX_TRY(decode(inStream, value.archive_secret_counter));
            }
            break;

            case 4:
            {
                if (parsed & 0x08)
                {
                    return errc::required_object_property_missing;
                }
                parsed |= 0x08;
                DPLX_TRY(decode(inStream, value.journal_counter));
            }
            break;

            default:
                return errc::unknown_property;
            }
        }
        return success();
    }
};

template <output_stream Stream>
class basic_encoder<vefs::detail::archive_header, Stream>
{
public:
    using value_type = vefs::detail::archive_header;
    inline auto operator()(Stream &outStream, value_type const &value) const
            -> result<void>
    {
        using emit = item_emitter<Stream>;

        DPLX_TRY(emit::map(outStream, 5u));
        DPLX_TRY(emit::integer(outStream, 0u)); // version prop
        DPLX_TRY(emit::integer(outStream, 0u)); // version 0

        DPLX_TRY(emit::integer(outStream, 1u));
        DPLX_TRY(encode(outStream, value.filesystem_index));

        DPLX_TRY(emit::integer(outStream, 2u));
        DPLX_TRY(encode(outStream, value.free_sector_index));

        DPLX_TRY(emit::integer(outStream, 3u));
        DPLX_TRY(encode(outStream, value.archive_secret_counter));

        DPLX_TRY(emit::integer(outStream, 4u));
        DPLX_TRY(encode(outStream, value.journal_counter));

        return success();
    }
};
} // namespace dplx::dp
