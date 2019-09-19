// Copyright 2018-2019 Henrik Steffen Gaßmann
//
#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>

#include <array>
#include <type_traits>

#include <boost/endian/conversion.hpp>

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
        auto read(std::size_t offset) const noexcept -> std::remove_cv_t<std::remove_reference_t<T>>
        {
            using type = std::remove_cv_t<std::remove_reference_t<T>>;
            static_assert(std::is_integral_v<type> || std::is_enum_v<type>);

            assert(offset + sizeof(type) <= mBuffer.size_bytes());

            using namespace boost::endian;

            if constexpr (std::is_same_v<type, std::byte>)
            {
                return mBuffer[offset];
            }
            else
            {
                type mem;
                std::memcpy(&mem, mBuffer.data() + offset, sizeof(type));
                return little_to_native(mem);
            }
        }

        template <typename T>
        void write(T value, std::size_t offset) noexcept
        {
            using type = std::remove_cv_t<std::remove_reference_t<T>>;
            static_assert(std::is_integral_v<type> || std::is_enum_v<type>);

            assert(offset + sizeof(type) <= mBuffer.size_bytes());

            using namespace boost::endian;

            if constexpr (std::is_same_v<type, std::byte>)
            {
                mBuffer[offset] = value;
            }
            else
            {
                type mem{native_to_little<type>(value)};
                std::memcpy(mBuffer.data() + offset, &mem, sizeof(type));
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
