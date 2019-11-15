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

namespace vefs::utils
{
    template <std::size_t Extent = dynamic_extent>
    class binary_codec final
    {
    public:
        binary_codec() = delete;
        binary_codec(span<std::byte, Extent> buffer)
            : mBuffer(buffer)
        {
        }

        template <typename T>
        auto read(std::size_t offset) const noexcept
            -> std::remove_cv_t<std::remove_reference_t<T>>
        {
            using type = utils::remove_cvref_t<T>;
            static_assert(std::is_integral_v<type> || std::is_enum_v<type>);
            using underlying_type =
                typename std::conditional_t<std::is_enum_v<type>,
                                            std::underlying_type<type>,
                                            boost::type_identity<type>>::type;

            assert(offset + sizeof(underlying_type) <= mBuffer.size_bytes());

            using boost::endian::little_to_native;

            if constexpr (std::is_same_v<type, std::byte>)
            {
                return mBuffer[offset];
            }
            else
            {
                underlying_type mem;
                std::memcpy(&mem, mBuffer.data() + offset,
                            sizeof(underlying_type));
                return type{little_to_native(mem)};
            }
        }

        template <typename T>
        void write(T value, std::size_t offset) noexcept
        {
            using type = std::remove_cv_t<std::remove_reference_t<T>>;
            static_assert(std::is_integral_v<type> || std::is_enum_v<type>);
            using underlying_type =
                typename std::conditional_t<std::is_enum_v<type>,
                                            std::underlying_type<type>,
                                            boost::type_identity<type>>::type;

            assert(offset + sizeof(type) <= mBuffer.size_bytes());

            using boost::endian::native_to_little;

            if constexpr (std::is_same_v<type, std::byte>)
            {
                mBuffer[offset] = value;
            }
            else
            {
                underlying_type mem{native_to_little<underlying_type>(
                    static_cast<underlying_type>(value))};
                std::memcpy(mBuffer.data() + offset, &mem,
                            sizeof(underlying_type));
            }
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
        span<std::byte, Extent> mBuffer;
    };
} // namespace vefs::utils
