#pragma once

#include <cstddef>

#include <array>
#include <span>

#include <vefs/platform/secure_memzero.hpp>

namespace vefs::utils
{

template <typename T, std::size_t arr_size>
struct secure_array : private std::array<T, arr_size>
{
private:
    using base_type = std::array<T, arr_size>;

public:
    static constexpr std::size_t static_size = arr_size;

    using span_type = std::span<T, static_size>;

    using typename base_type::const_iterator;
    using typename base_type::const_pointer;
    using typename base_type::const_reference;
    using typename base_type::const_reverse_iterator;
    using typename base_type::difference_type;
    using typename base_type::iterator;
    using typename base_type::pointer;
    using typename base_type::reference;
    using typename base_type::reverse_iterator;
    using typename base_type::size_type;
    using typename base_type::value_type;

    secure_array() noexcept = default;
    explicit secure_array(std::span<const T, arr_size> other) noexcept
    {
        copy(other, as_span(*this));
    }
    secure_array(secure_array const &) noexcept = default;
    secure_array(secure_array &&other) noexcept
        : base_type(other)
    {
        secure_memzero(as_writable_bytes(span_type{other}));
    }
    ~secure_array()
    {
        secure_memzero(as_writable_bytes(span_type{*this}));
    }

    auto operator=(secure_array const &) noexcept -> secure_array & = default;
    auto operator=(secure_array &&other) noexcept -> secure_array &
    {
        static_cast<base_type &>(*this) = static_cast<base_type &>(other);
        secure_memzero(as_writable_bytes(span_type{other}));
        return *this;
    }

    using base_type::at;
    using base_type::operator[];
    using base_type::back;
    using base_type::data;
    using base_type::front;

    using base_type::begin;
    using base_type::cbegin;
    using base_type::cend;
    using base_type::crbegin;
    using base_type::crend;
    using base_type::end;
    using base_type::rbegin;
    using base_type::rend;

    using base_type::empty;
    using base_type::max_size;
    using base_type::size;

    using base_type::fill;
};

template <typename T, std::size_t arr_size>
constexpr auto as_span(secure_array<T, arr_size> &arr) noexcept
{
    return std::span<T, arr_size>(arr);
}
template <typename T, std::size_t arr_size>
constexpr auto as_span(secure_array<T, arr_size> const &arr) noexcept
{
    return std::span<T const, arr_size>(arr);
}

template <std::size_t arr_size>
using secure_byte_array = secure_array<std::byte, arr_size>;

} // namespace vefs::utils
