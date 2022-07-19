// Copyright 2016-2017 Henrik Steffen Gaßmann
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// Limitations under the License.
// /////////////////////////////////////////////////////////////////////////////
#pragma once

#include <type_traits>

template <typename E>
std::false_type allow_enum_bitset(E &&);

template <typename E>
constexpr bool enum_bitsets_allowed
        = std::is_enum_v<E> &&decltype(allow_enum_bitset(
                std::declval<E>()))::value;

/**
 * Wrapper for enums to enable bitoperations
 * @tparam E enum that is wrapped
 */
template <typename E>
class enum_bitset
{
public:
    static_assert(enum_bitsets_allowed<E>);

    using enum_type = E;
    using underlying_type = std::underlying_type_t<enum_type>;

    constexpr enum_bitset() = default;
    constexpr enum_bitset(enum_type value)
        : mValue(static_cast<underlying_type>(value))
    {
    }
    constexpr explicit enum_bitset(underlying_type value)
        : mValue(value)
    {
    }

    constexpr explicit operator bool() const
    {
        return mValue != underlying_type{};
    }

    constexpr bool operator==(enum_bitset rhs) const
    {
        return mValue == rhs.mValue;
    }
    constexpr bool operator!=(enum_bitset rhs) const
    {
        return mValue != rhs.mValue;
    }

    constexpr bool operator%(enum_bitset testValue) const
    {
        return (*this & testValue) == testValue;
    }
    constexpr bool test(enum_bitset value) const
    {
        return *this % value;
    }

    constexpr enum_bitset operator&(enum_bitset rhs) const
    {
        return enum_bitset{mValue & rhs.mValue};
    }
    constexpr enum_bitset &operator&=(enum_bitset rhs)
    {
        mValue &= rhs.mValue;
        return *this;
    }

    constexpr enum_bitset operator|(enum_bitset rhs) const
    {
        return enum_bitset{mValue | rhs.mValue};
    }
    constexpr enum_bitset &operator|=(enum_bitset rhs)
    {
        mValue |= rhs.mValue;
        return *this;
    }

    constexpr enum_bitset operator^(enum_bitset rhs) const
    {
        return enum_bitset{mValue ^ rhs.mValue};
    }
    constexpr enum_bitset &operator^=(enum_bitset rhs)
    {
        mValue ^= rhs.mValue;
        return *this;
    }

    constexpr enum_bitset operator~() const
    {
        return enum_bitset{~mValue};
    }

private:
    underlying_type mValue;
};

template <class E, std::enable_if_t<enum_bitsets_allowed<E>, int> = 0>
constexpr enum_bitset<E> operator|(E lhs, E rhs)
{
    using bitset_type = ::enum_bitset<E>;
    using value_type = typename bitset_type::underlying_type;
    return bitset_type{static_cast<value_type>(lhs)
                       | static_cast<value_type>(rhs)};
}
template <class E, std::enable_if_t<enum_bitsets_allowed<E>, int> = 0>
constexpr enum_bitset<E> operator|(E lhs, enum_bitset<E> rhs)
{
    return rhs | lhs;
}

template <class E, std::enable_if_t<enum_bitsets_allowed<E>, int> = 0>
constexpr enum_bitset<E> operator&(E lhs, E rhs)
{
    using bitset_type = ::enum_bitset<E>;
    using value_type = typename bitset_type::underlying_type;
    return bitset_type{static_cast<value_type>(lhs)
                       & static_cast<value_type>(rhs)};
}
template <class E, std::enable_if_t<enum_bitsets_allowed<E>, int> = 0>
constexpr enum_bitset<E> operator&(E lhs, enum_bitset<E> rhs)
{
    return rhs & lhs;
}

template <class E, std::enable_if_t<enum_bitsets_allowed<E>, int> = 0>
constexpr enum_bitset<E> operator^(E lhs, E rhs)
{
    using bitset_type = ::enum_bitset<E>;
    using value_type = typename bitset_type::underlying_type;
    return bitset_type{static_cast<value_type>(lhs)
                       ^ static_cast<value_type>(rhs)};
}
template <class E, std::enable_if_t<enum_bitsets_allowed<E>, int> = 0>
constexpr enum_bitset<E> operator^(E lhs, enum_bitset<E> rhs)
{
    return rhs ^ lhs;
}
