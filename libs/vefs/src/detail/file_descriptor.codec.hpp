#pragma once

#include <span>

#include <dplx/dp/decoder/api.hpp>
#include <dplx/dp/decoder/core.hpp>
#include <dplx/dp/decoder/std_container.hpp>
#include <dplx/dp/decoder/std_string.hpp>
#include <dplx/dp/encoder/api.hpp>
#include <dplx/dp/encoder/core.hpp>

#include "../crypto/counter.codec.hpp"
#include "cbor_utils.hpp"
#include "file_descriptor.hpp"

namespace dplx::dp
{
    template <output_stream Stream>
    class basic_encoder<vefs::detail::file_descriptor, Stream>
    {
    public:
        inline auto operator()(Stream &outStream,
                               vefs::detail::file_descriptor const &value) const
            -> result<void>
        {
            using emit = item_emitter<Stream>;

            DPLX_TRY(emit::map(outStream, 9u));

            DPLX_TRY(emit::integer(outStream, 1u));
            DPLX_TRY(encode(outStream, as_bytes(std::span(value.fileId.data))));

            DPLX_TRY(emit::integer(outStream, 2u));
            DPLX_TRY(encode(outStream, value.filePath));

            DPLX_TRY(emit::integer(outStream, 3u));
            DPLX_TRY(encode(outStream, value.secret));

            DPLX_TRY(emit::integer(outStream, 4u));
            DPLX_TRY(encode(outStream, value.secretCounter));

            DPLX_TRY(emit::integer(outStream, 5u));
            DPLX_TRY(encode(
                outStream, static_cast<std::uint64_t>(value.data.root.sector)));

            DPLX_TRY(emit::integer(outStream, 6u));
            DPLX_TRY(encode(outStream, value.data.root.mac));

            DPLX_TRY(emit::integer(outStream, 7u));
            DPLX_TRY(encode(outStream, value.data.maximum_extent));

            DPLX_TRY(emit::integer(outStream, 8u));
            DPLX_TRY(encode(outStream, value.data.tree_depth));

            DPLX_TRY(emit::integer(outStream, 9u));
            DPLX_TRY(encode(outStream, value.modificationTime));

            return success();
        }
    };

    template <input_stream Stream>
    class basic_decoder<vefs::detail::file_descriptor, Stream>
    {
    public:
        inline auto operator()(Stream &inStream,
                               vefs::detail::file_descriptor &value) const
            -> result<void>
        {
            DPLX_TRY(auto &&headInfo,
                     dp::parse_object_head(inStream, std::false_type{}));

            if (headInfo.num_properties != 9)
            {
                return errc::item_version_mismatch;
            }
            unsigned int parsed = 0;
            for (auto i = 0; i < 9; ++i)
            {
                DPLX_TRY(auto &&info, detail::parse_item_info(inStream));
                if (std::byte{info.type} != type_code::posint)
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
                    auto fileId =
                        as_writable_bytes(std::span(value.fileId.data));
                    DPLX_TRY(decode(inStream, fileId));
                }
                break;
                case 2:
                {
                    if (parsed & 0x02)
                    {
                        return errc::required_object_property_missing;
                    }
                    parsed |= 0x02;
                    DPLX_TRY(decode(inStream, value.filePath));
                }
                break;
                case 3:
                {
                    if (parsed & 0x04)
                    {
                        return errc::required_object_property_missing;
                    }
                    parsed |= 0x04;
                    DPLX_TRY(decode(inStream, value.secret));
                }
                break;
                case 4:
                {
                    if (parsed & 0x08)
                    {
                        return errc::required_object_property_missing;
                    }
                    DPLX_TRY(decode(inStream, value.secretCounter));
                    parsed |= 0x08;
                }
                break;
                case 5:
                    if (parsed & 0x10)
                    {
                        return errc::required_object_property_missing;
                    }
                    {
                        DPLX_TRY(auto rootValue,
                                 decode(as_value<std::uint64_t>, inStream));

                        value.data.root.sector =
                            static_cast<vefs::detail::sector_id>(rootValue);
                    }
                    parsed |= 0x10;
                    break;
                case 6:
                {
                    if (parsed & 0x20)
                    {
                        return errc::required_object_property_missing;
                    }
                    DPLX_TRY(decode(inStream, value.data.root.mac));
                    parsed |= 0x20;
                }
                break;
                case 7:
                {
                    if (parsed & 0x40)
                    {
                        return errc::required_object_property_missing;
                    }
                    DPLX_TRY(decode(inStream, value.data.maximum_extent));
                    parsed |= 0x40;
                }
                break;
                case 8:
                {
                    if (parsed & 0x80)
                    {
                        return errc::required_object_property_missing;
                    }
                    DPLX_TRY(decode(inStream, value.data.tree_depth));
                    parsed |= 0x80;
                }
                break;
                case 9:
                {
                    if (parsed & 0x100)
                    {
                        return errc::required_object_property_missing;
                    }
                    DPLX_TRY(decode(inStream, value.modificationTime));
                    parsed |= 0x100;
                }
                break;

                default:
                    return errc::unknown_property;
                }
            }
            return success();
        }
    };

    inline auto tag_invoke(encoded_size_of_fn,
                           vefs::detail::file_descriptor const &entry) noexcept
        -> unsigned int
    {
        using dplx::dp::detail::var_uint_encoded_size;

        auto const mapPrefix = var_uint_encoded_size(9u);

        auto const idSize =
            encoded_size_of(1u) +
            encoded_size_of(as_bytes(std::span(entry.fileId.data)));

        auto const pathSize =
            encoded_size_of(2u) + encoded_size_of(entry.filePath);

        auto const secretSize =
            encoded_size_of(3u) + encoded_size_of(entry.secret);

        auto const secretCounterSize =
            encoded_size_of(4u) + encoded_size_of(entry.secretCounter.view());

        auto const rootSectorSize =
            encoded_size_of(5u) +
            encoded_size_of(static_cast<std::uint64_t>(entry.data.root.sector));

        auto const rootMacSize =
            encoded_size_of(6u) + encoded_size_of(entry.data.root.mac);

        auto const maxExtentSize =
            encoded_size_of(7u) + encoded_size_of(entry.data.maximum_extent);

        auto const treeDepthSize =
            encoded_size_of(8u) +
            encoded_size_of(static_cast<unsigned>(entry.data.tree_depth));

        auto const modTimeSize =
            encoded_size_of(9u) + encoded_size_of(entry.modificationTime);

        return mapPrefix + idSize + pathSize + secretSize + secretCounterSize +
               rootSectorSize + rootMacSize + maxExtentSize + treeDepthSize +
               modTimeSize;
    }

} // namespace dplx::dp
