// Copyright 2018-2019 Henrik Steffen Gaßmann
//
#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>

#include <array>
#include <type_traits>

#include <boost/endian/conversion.hpp>

#include <vefs/blob.hpp>

namespace vefs::utils
{                  
    class binary_codec final
    {
    public:
        binary_codec() = delete;
        binary_codec(rw_dynblob buffer)
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

            type mem;
            std::memcpy(&mem, mBuffer.data() + offset, sizeof(type));
            return little_to_native(mem);
        }

        template <typename T>
        void write(T value, std::size_t offset) noexcept
        {
            using type = std::remove_cv_t<std::remove_reference_t<T>>;
            static_assert(std::is_integral_v<type> || std::is_enum_v<type>);

            assert(offset + sizeof(type) <= mBuffer.size_bytes());

            using namespace boost::endian;

            type mem{native_to_little<type>(value)};
            std::memcpy(mBuffer.data() + offset, &mem, sizeof(type));
        }

        void write(std::byte value, std::size_t offset) noexcept
        {
            assert(offset < mBuffer.size_bytes());
            mBuffer[offset] = value;
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
        rw_dynblob mBuffer;
    };

    template <>
    inline auto binary_codec::read<std::byte>(std::size_t offset) const noexcept -> std::byte
    {
        assert(offset < mBuffer.size_bytes());
        return mBuffer[offset];
    }
    template <>
    inline void binary_codec::write<std::byte>(std::byte value, std::size_t offset) noexcept
    {
        write(value, offset);
    }
} // namespace dcurve::ipc::detail
