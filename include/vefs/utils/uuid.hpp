#pragma once

#include <uuid.h>

namespace vefs
{

using uuid = uuids::uuid;

namespace detail
{

template <typename CharT>
inline constexpr CharT guid_encoding_lut[18] = {};

template <>
inline constexpr char guid_encoding_lut<char>[18] = "0123456789abcdef-";
template <>
inline constexpr wchar_t guid_encoding_lut<wchar_t>[18] = L"0123456789abcdef-";
template <>
inline constexpr char8_t guid_encoding_lut<char8_t>[18] = u8"0123456789abcdef-";
template <>
inline constexpr char16_t guid_encoding_lut<char16_t>[18]
        = u"0123456789abcdef-";
template <>
inline constexpr char32_t guid_encoding_lut<char32_t>[18]
        = U"0123456789abcdef-";

} // namespace detail

} // namespace vefs
