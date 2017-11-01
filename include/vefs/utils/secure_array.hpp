#pragma once

#include <cstddef>

#include <array>

#include <vefs/blob.hpp>
#include <vefs/utils/secure_ops.hpp>

namespace vefs::utils
{
    template <typename T, std::size_t arr_size>
    struct secure_array
        : private std::array<T, arr_size>
    {
    private:
        using base_type = std::array<T, arr_size>;

    public:
        static constexpr std::size_t static_size = arr_size;

        using base_type::value_type;
        using base_type::size_type;
        using base_type::difference_type;
        using base_type::reference;
        using base_type::const_reference;
        using base_type::pointer;
        using base_type::const_pointer;
        using base_type::iterator;
        using base_type::const_iterator;
        using base_type::reverse_iterator;
        using base_type::const_reverse_iterator;

        secure_array() = default;
        secure_array(const secure_array &) = default;
        secure_array(secure_array &&other)
            : base_type(other)
        {
            secure_memzero(blob{ other });
        }
        ~secure_array()
        {
            secure_memzero(blob{ *this });
        }

        secure_array & operator=(const secure_array &) = default;
        secure_array & operator=(secure_array &&other)
        {
            *this = static_cast<base_type &>(other);
            secure_memzero(blob{ other });
            return this;
        }

        using base_type::at;
        using base_type::operator[];
        using base_type::front;
        using base_type::back;
        using base_type::data;

        using base_type::begin;
        using base_type::cbegin;
        using base_type::rbegin;
        using base_type::crbegin;
        using base_type::end;
        using base_type::rend;
        using base_type::cend;
        using base_type::crend;

        using base_type::empty;
        using base_type::size;
        using base_type::max_size;

        using base_type::fill;
    };

    template <std::size_t arr_size>
    using secure_byte_array = secure_array<std::byte, arr_size>;
}
