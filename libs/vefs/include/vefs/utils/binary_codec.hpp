// Copyright 2018-2019 Henrik Steffen Gaßmann
//
#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>

#include <array>
#include <type_traits>

#include <boost/endian/conversion.hpp>
#include <boost/type_traits/type_identity.hpp>

#include <vefs/span.hpp>
#include <vefs/utils/misc.hpp>

namespace vefs
{
template <typename T, std::size_t Extent>
inline auto load_primitive(std::span<const std::byte, Extent> memory,
                           std::size_t offset = 0) noexcept
        -> utils::remove_cvref_t<T>
{
    using type = utils::remove_cvref_t<T>;
    static_assert(std::is_integral_v<type> || std::is_enum_v<type>);
    using underlying_type =
            typename std::conditional_t<std::is_enum_v<type>,
                                        std::underlying_type<type>,
                                        boost::type_identity<type>>::type;

    assert(offset + sizeof(underlying_type) <= memory.size_bytes());

    using boost::endian::little_to_native;

    if constexpr (std::is_same_v<type, std::byte>)
    {
        return memory[offset];
    }
    else
    {
        underlying_type stored;
        std::memcpy(&stored, memory.data() + offset, sizeof(underlying_type));
        return type{little_to_native(stored)};
    }
}

template <typename T, std::size_t Extent>
inline auto load_primitive(std::span<std::byte, Extent> memory,
                           std::size_t offset = 0) noexcept
{
    return load_primitive<T>(std::span<const std::byte, Extent>(memory),
                             offset);
}

template <typename T, std::size_t Extent>
inline void store_primitive(std::span<std::byte, Extent> memory,
                            T value,
                            std::size_t offset = 0) noexcept
{
    using type = std::remove_cv_t<std::remove_reference_t<T>>;
    static_assert(std::is_integral_v<type> || std::is_enum_v<type>);
    using underlying_type =
            typename std::conditional_t<std::is_enum_v<type>,
                                        std::underlying_type<type>,
                                        boost::type_identity<type>>::type;

    assert(offset + sizeof(type) <= memory.size_bytes());

    using boost::endian::native_to_little;

    if constexpr (std::is_same_v<type, std::byte>)
    {
        memory[offset] = value;
    }
    else
    {
        auto stored = native_to_little(static_cast<underlying_type>(value));
        std::memcpy(memory.data() + offset, &stored, sizeof(underlying_type));
    }
}
} // namespace vefs

namespace vefs::utils
{

template <std::size_t Extent = dynamic_extent>
class binary_codec final
{
public:
    binary_codec() = delete;
    binary_codec(std::span<std::byte, Extent> buffer)
        : mBuffer(buffer)
    {
    }

    template <typename T>
    auto read(std::size_t offset) const noexcept -> remove_cvref_t<T>
    {
        return load_primitive<T>(mBuffer, offset);
    }

    template <typename T>
    void write(T value, std::size_t offset) noexcept
    {
        return store_primitive(mBuffer, value, offset);
    }

    auto as_bytes() const noexcept -> ro_dynblob
    {
        return mBuffer;
    }
    auto as_writeable_bytes() noexcept -> rw_dynblob
    {
        return mBuffer;
    }
    auto data() noexcept -> std::byte *
    {
        return mBuffer.data();
    }
    auto data() const noexcept -> const std::byte *
    {
        return mBuffer.data();
    }
    auto size() const noexcept -> std::size_t
    {
        return mBuffer.size_bytes();
    }

private:
    std::span<std::byte, Extent> mBuffer;
};
} // namespace vefs::utils
