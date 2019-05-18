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
#include "precompiled.hpp"
#include "utf.hpp"


using namespace utf::detail;

namespace utf
{
    std::tuple<char32_t, ptrdiff_t> decode(std::string_view src)
    {
        if (src.empty())
        {
            BOOST_THROW_EXCEPTION(not_enough_room_exception()
                << sequence_length_info(0));
        }
        const size_t seqLength = sequence_length(src.front());
        if (!seqLength)
        {
            BOOST_THROW_EXCEPTION(invalid_utf8_sequence_exception()
                << sequence_length_info(1));
        }
        if (seqLength > src.size())
        {
            BOOST_THROW_EXCEPTION(incomplete_utf8_sequence_exception()
                << sequence_length_info(src.size()));
        }

        char32_t cp = ERROR_CHAR;
        size_t invalid_size = 1;
        switch (seqLength)
        {
        case 1: cp = get_sequence<1, true>(src.data(), &invalid_size); break;
        case 2: cp = get_sequence<2, true>(src.data(), &invalid_size); break;
        case 3: cp = get_sequence<3, true>(src.data(), &invalid_size); break;
        case 4: cp = get_sequence<4, true>(src.data(), &invalid_size); break;
        default: break; // this cannot happen, due to the exception condition above
        }

        if (cp == ERROR_CHAR)
        {
            BOOST_THROW_EXCEPTION(incomplete_utf8_sequence_exception()
                << sequence_length_info(invalid_size));
        }
        if (!is_code_point_valid(cp))
        {
            BOOST_THROW_EXCEPTION(invalid_code_point_exception()
                << code_point_info(cp)
                << sequence_length_info(seqLength));
        }
        if (seqLength != encoded_utf8_size(cp))
        {
            BOOST_THROW_EXCEPTION(overlong_utf8_sequence_exception()
                << sequence_length_info(seqLength));
        }
        return { cp, seqLength };
    }

    ptrdiff_t find_invalid(std::string_view src)
    {
        std::size_t origSize = src.size();
        while (!src.empty())
        {
            const auto seqLength = sequence_length(src.front());
            if (seqLength >= src.size())
            {
                return origSize - src.size();
            }

            char32_t cp = ERROR_CHAR;
            switch (seqLength)
            {
            case 1: cp = get_sequence<1, true>(src.data()); break;
            case 2: cp = get_sequence<2, true>(src.data()); break;
            case 3: cp = get_sequence<3, true>(src.data()); break;
            case 4: cp = get_sequence<4, true>(src.data()); break;
            default: break;
            }

            if (cp == ERROR_CHAR
                || !is_code_point_valid(cp)
                || seqLength != encoded_utf8_size(cp))
            {
                return origSize - src.size();
            }
            src.remove_prefix(seqLength);
        }
        return -1;
    }

    std::u16string utf8_to_utf16(std::string_view src)
    {
        std::u16string conv;
        conv.reserve(src.size() * 2);

        while (!src.empty())
        {
            auto[cp, offset] = decode(src);

            if (cp <= 0xFFFF)
            {
                conv.push_back(static_cast<char16_t>(cp));
            }
            else
            {
                conv.push_back(encode_surrogate_lead(cp));
                conv.push_back(encode_surrogate_trail(cp));
            }

            src.remove_prefix(static_cast<std::size_t>(offset));
        }

        return conv;
    }

    size_t utf8_to_utf16(std::string_view src, char16_t* dest, const char16_t* destEnd)
    {
        char16_t *out = dest;
        while (!src.empty())
        {
            auto[cp, offset] = decode(src);

            const size_t u16len = 1 + (cp > 0xFFFF);
            char16_t *nextDest = out + u16len;
            if (nextDest > destEnd)
            {
                BOOST_THROW_EXCEPTION(not_enough_room_exception());
            }

            switch (u16len)
            {
            case 1:
                out[0] = static_cast<char16_t>(cp);
                break;
            case 2:
                out[0] = encode_surrogate_lead(cp);
                out[1] = encode_surrogate_trail(cp);
                break;
            }
            out = nextDest;

            src.remove_prefix(static_cast<std::size_t>(offset));
        }
        return out - dest;
    }
}
