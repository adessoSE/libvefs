// Copyright 2016-2017 Henrik Steffen Gaßmann
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//		http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstddef>
#include <cstdint>

#include <tuple>
#include <string>
#include <exception>
#include <type_traits>
#include <string_view>

#include <boost/exception/error_info.hpp>
#include <boost/exception/exception.hpp>
#include <boost/exception/info.hpp>
#include <boost/throw_exception.hpp>

namespace utf
{
    class exception
        : public virtual std::exception
        , public virtual boost::exception
    {
    };

    class invalid_code_point_exception
        : public virtual exception
    {
    };

    class not_enough_room_exception
        : public virtual exception
    {
    };

    class invalid_utf8_sequence_exception
        : public virtual exception
    {
    };

    class overlong_utf8_sequence_exception
        : public virtual invalid_utf8_sequence_exception
    {
    };

    class incomplete_utf8_sequence_exception
        : public virtual invalid_utf8_sequence_exception
    {
    };

    using code_point_info = boost::error_info<struct tag_code_point, char32_t>;
    using sequence_length_info = boost::error_info<struct tag_sequence_length, size_t>;

    namespace detail
    {
        constexpr char encode_char(std::uint8_t byteValue)
        {
            return static_cast<char>(byteValue);
        }


        // Unicode constants
        constexpr char32_t CODE_POINT_MAX = 0x10FFFF;
        constexpr char32_t ERROR_CHAR = 0xFFFFFFFF;

        // high surrogates: 0xd800 - 0xdbff
        // low surrogates:  0xdc00 - 0xdfff
        constexpr char16_t SURROGATE_LEAD_MIN = 0xD800;
        constexpr char16_t SURROGATE_LEAD_MAX = 0xDBFF;
        constexpr char16_t SURROGATE_TRAIL_MIN = 0xDC00;
        constexpr char16_t SURROGATE_TRAIL_MAX = 0xDFFF;
        constexpr char16_t SURROGATE_LEAD_OFFSET = SURROGATE_LEAD_MIN - (0x10000 >> 10);

        // msvc truncates char values > 127 Ò.Ó
        constexpr char bom[] = { encode_char(0xEF), encode_char(0xBB), encode_char(0xBF) };

        constexpr bool starts_with_bom(std::string_view str)
        {
            return str.size() >= 3
                && str[0] == bom[0]
                && str[1] == bom[1]
                && str[2] == bom[2];
        }

        // the number of characters which should follow the given lead code unit
        constexpr std::uint8_t lead_char_class[] = {
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
            3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
            4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0
        };

        constexpr size_t sequence_length(char leadUnit) noexcept
        {
            return lead_char_class[static_cast<unsigned char>(leadUnit)];
        }

        constexpr bool is_lead(char codeUnit) noexcept
        {
            return sequence_length(codeUnit) != 0;
        }

        constexpr bool is_trail(char codeUnit) noexcept
        {
            return static_cast<unsigned char>(codeUnit) >> 6 == 0x2;
        }

        constexpr bool is_lead_surrogate(char32_t cp) noexcept
        {
            return SURROGATE_LEAD_MIN <= cp && cp <= SURROGATE_LEAD_MAX;
        }

        constexpr bool is_trail_surrogate(char32_t cp) noexcept
        {
            return SURROGATE_TRAIL_MIN <= cp && cp <= SURROGATE_TRAIL_MAX;
        }

        constexpr bool is_surrogate(char32_t cp) noexcept
        {
            return SURROGATE_LEAD_MIN <= cp && cp <= SURROGATE_TRAIL_MAX;
        }

        constexpr bool is_code_point_valid(char32_t cp) noexcept
        {
            return cp <= CODE_POINT_MAX && !is_surrogate(cp);
        }


        constexpr char16_t encode_surrogate_lead(char32_t cp) noexcept
        {
            return static_cast<char16_t>((cp >> 10) + SURROGATE_LEAD_OFFSET);
        }

        constexpr char16_t encode_surrogate_trail(char32_t cp) noexcept
        {
            return static_cast<char16_t>(static_cast<char16_t>((cp & 0x3ff) + SURROGATE_TRAIL_MIN));
        }

        constexpr size_t encoded_utf8_size(char32_t cp) noexcept
        {
            constexpr size_t encoded_utf8_size_map[] = { 1, 2, 3, 4, 0 };

            return encoded_utf8_size_map[
                0u + (cp >= 0x80) + (cp >= 0x800) + (cp >= 0x10000) + (cp > CODE_POINT_MAX)
            ];
        }

        template<size_t seq_length>
        constexpr void encode_impl(char32_t cp, char *output)
        {
            static_assert(1 < seq_length && seq_length < 5, "utf8 sequences are 1 to 4 bytes long");

            output[0] = static_cast<char>(
                cp >> ((seq_length - 1) * 6) | ((0x3C0 >> (seq_length - 2)) & 0xF0));
            for (size_t i = 1; i < seq_length; ++i)
            {
                output[i] = static_cast<char>(((cp >> ((seq_length - 1 - i) * 6)) & 0x3F) | 0x80);
            }
        }

        template< >
        constexpr void encode_impl<1>(char32_t cp, char *output)
        {
            output[0] = static_cast<char>(cp & 0x7F);
        }

        constexpr ptrdiff_t encode_unsafe(char32_t cp, char *output)
        {
            if (cp < 0x80)
                return encode_impl<1>(cp, output), 1;
            if (cp < 0x800)
                return encode_impl<2>(cp, output), 2;
            if (cp < 0x10000)
                return encode_impl<3>(cp, output), 3;
            else
                return encode_impl<4>(cp, output), 4;
        }

        ptrdiff_t encode(char32_t cp, char *output, char *end);

        template< int seq_length, bool checked >
        constexpr char32_t get_sequence(const char *src,
            [[maybe_unused]] size_t *invalid_size = nullptr)
        {
            static_assert(0 < seq_length && seq_length < 5, "utf8 sequences are 1 to 4 bytes long");

            // the first byte has to be readable, otherwise you could not know the length
            if constexpr (seq_length == 1)
            {
                return static_cast<char32_t>(src[0]);
            }
            else
            {
                char32_t cp = static_cast<char32_t>(src[0] & (0x7F >> seq_length));
                cp <<= (seq_length - 1) * 6;
                for (ptrdiff_t i = seq_length - 1; i != 0; )
                {
                    const char tmp = src[seq_length - i];
                    if constexpr (checked)
                    {
                        if (BOOST_UNLIKELY(!is_trail(tmp)))
                        {
                            if (invalid_size)
                            {
                                *invalid_size = seq_length - i;
                            }
                            return ERROR_CHAR;
                        }
                    }
                    cp |= static_cast<char32_t>(static_cast<unsigned char>(tmp) & 0x3F) << --i * 6;
                }
                return cp;
            }
        }

        constexpr std::tuple<char32_t, ptrdiff_t> decode_unsafe(const char *src)
        {
            const ptrdiff_t seqLength = sequence_length(*src);
            switch (seqLength)
            {
            case 1: return { get_sequence<1, false>(src), 1 };
            case 2: return { get_sequence<2, false>(src), 2 };
            case 3: return { get_sequence<3, false>(src), 3 };
            case 4: return { get_sequence<4, false>(src), 4 };
            default: return { ERROR_CHAR, 1 };
            }
        }

        constexpr ptrdiff_t previous_offset_unsafe(const char *src)
        {
            const char *it = src;
            while (is_trail(*--it))
                ;
            return it - src;
        }
    }

    std::tuple<char32_t, ptrdiff_t> decode(std::string_view src);

    ptrdiff_t find_invalid(std::string_view src);

    inline bool is_valid(std::string_view src)
    {
        return find_invalid(src) == -1;
    }

    std::u16string utf8_to_utf16(std::string_view src);
    size_t utf8_to_utf16(std::string_view src, char16_t *dest, const char16_t *destEnd);
}
