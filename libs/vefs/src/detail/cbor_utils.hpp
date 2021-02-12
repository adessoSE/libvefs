#pragma once

#include <cstdint>

#include <limits>
#include <type_traits>

#include <dplx/dp/disappointment.hpp>
#include <dplx/dp/item_parser.hpp>
#include <dplx/dp/stream.hpp>

namespace dplx::dp
{
inline constexpr std::uint32_t null_def_version = 0xffff'ffffu;

struct tuple_head_info
{
    std::int32_t num_properties;
    std::uint32_t version;
};

template <input_stream Stream, bool isVersioned = false>
inline auto parse_tuple_head(Stream &inStream,
                             std::bool_constant<isVersioned> = {})
        -> result<tuple_head_info>
{
    DPLX_TRY(auto &&arrayInfo, detail::parse_item_info(inStream));
    if (std::byte{arrayInfo.type} != type_code::array)
    {
        return errc::item_type_mismatch;
    }

    DPLX_TRY(auto &&remainingBytes, available_input_size(inStream));
    if (arrayInfo.value > remainingBytes)
    {
        return errc::end_of_stream;
    }
    if (arrayInfo.value
        >= static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
    {
        return errc::too_many_properties;
    }
    auto numProps = static_cast<std::int32_t>(arrayInfo.value);

    if constexpr (!isVersioned)
    {
        return tuple_head_info{numProps, null_def_version};
    }
    else
    {
        if (arrayInfo.value < 1u)
        {
            return errc::item_version_property_missing;
        }

        DPLX_TRY(auto &&versionInfo, detail::parse_item_info(inStream));
        if (std::byte{versionInfo.type} != type_code::posint)
        {
            return errc::item_version_property_missing;
        }
        // 0xffff'ffff => max() is reserved as null_def_version
        if (versionInfo.value >= std::numeric_limits<std::uint32_t>::max())
        {
            return errc::item_value_out_of_range;
        }
        return tuple_head_info{numProps - 1,
                               static_cast<std::uint32_t>(versionInfo.value)};
    }
}

struct object_head_info
{
    std::int32_t num_properties;
    std::uint32_t version;
};

template <input_stream Stream, bool isVersioned = true>
inline auto parse_object_head(Stream &inStream,
                              std::bool_constant<isVersioned> = {})
        -> result<object_head_info>
{
    DPLX_TRY(auto &&mapInfo, detail::parse_item_info(inStream));
    if (static_cast<std::byte>(mapInfo.type & 0b111'00000) != type_code::map)
    {
        return errc::item_type_mismatch;
    }
    auto const indefinite = mapInfo.indefinite();
    if (!indefinite && mapInfo.value == 0)
    {
        return object_head_info{0, null_def_version};
    }

    DPLX_TRY(auto &&remainingBytes, available_input_size(inStream));
    // every prop consists of two items each being at least 1B big
    if (mapInfo.value > (remainingBytes / 2))
    {
        return errc::end_of_stream;
    }
    if (mapInfo.value >= static_cast<std::uint64_t>(
                std::numeric_limits<std::int32_t>::max() / 2))
    {
        return errc::too_many_properties;
    }
    auto numProps = static_cast<std::int32_t>(mapInfo.value);

    if constexpr (!isVersioned)
    {
        return object_head_info{numProps, null_def_version};
    }
    else
    {
        // the version property id is posint 0
        // and always encoded as a single byte
        DPLX_TRY(auto &&maybeVersionReadProxy, read(inStream, 1));
        if (std::ranges::data(maybeVersionReadProxy)[0] != std::byte{})
        {
            DPLX_TRY(consume(inStream, maybeVersionReadProxy, 0));
            return object_head_info{numProps, null_def_version};
        }

        if constexpr (lazy_input_stream<Stream>)
        {
            DPLX_TRY(consume(inStream, maybeVersionReadProxy));
        }

        DPLX_TRY(auto &&versionInfo, detail::parse_item_info(inStream));
        if (std::byte{versionInfo.type} != type_code::posint)
        {
            return errc::item_type_mismatch;
        }
        // 0xffff'ffff => max() is reserved as null_def_version
        if (versionInfo.value >= std::numeric_limits<std::uint32_t>::max())
        {
            return errc::item_value_out_of_range;
        }
        return object_head_info{numProps - 1,
                                static_cast<std::uint32_t>(versionInfo.value)};
    }
}
} // namespace dplx::dp
