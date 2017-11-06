#pragma once

#include <cstdint>
#include <cstdio>

#include <limits>
#include <string>
#include <ostream>
#include <iomanip>

#include <boost/io/ios_state.hpp>

namespace vefs::detail
{
    enum class sector_id : std::uint64_t
    {
        master = 0,
    };

    constexpr std::size_t sector_id_string_size
        = 2 + (std::numeric_limits< std::underlying_type_t<sector_id> >::digits >> 2);
    constexpr const char *sector_id_string_fmt = "0x%.16llx";

    inline bool operator<(sector_id lhs, sector_id rhs)
    {
        return static_cast<std::uint64_t>(lhs) < static_cast<std::uint64_t>(rhs);
    }

    template <class CharT, class Traits>
    inline std::basic_ostream<CharT, Traits> & operator<<(std::basic_ostream<CharT, Traits> &ostr, sector_id sid)
    {
        using raw_type = std::underlying_type_t<sector_id>;
        using limits = std::numeric_limits<raw_type>;
        constexpr auto hexDigits = limits::digits >> 2;
        constexpr auto fillChar = static_cast<CharT>('0');
        constexpr auto hexPrefix0 = static_cast<CharT>('0');
        constexpr auto hexPrefix1 = static_cast<CharT>('x');

        boost::io::ios_flags_saver flags{ ostr };
        boost::io::ios_width_saver width{ ostr };
        boost::io::ios_fill_saver fill{ ostr };

        return ostr << std::hex << std::right << std::setfill(fillChar) << std::setw(hexDigits)
            << hexPrefix0 << hexPrefix1 << static_cast<raw_type>(sid);
    }

    inline void to_string(sector_id id, std::string &out, std::size_t position = {})
    {
        char saved;
        const auto size = position + sector_id_string_size;
        bool append = out.size() <= size;
        if (append)
        {
            out.resize(size + 1, '\0');
        }
        else
        {
            saved = out[size];
        }

        snprintf(out.data() + position, out.size() - position,
            sector_id_string_fmt, static_cast<std::underlying_type_t<sector_id>>(id));

        if (append)
        {
            out.pop_back();
        }
        else
        {
            out[size] = saved;
        }
    }

    inline std::string to_string(sector_id id)
    {
        std::string buffer(sector_id_string_size + 1, '\0');

        snprintf(buffer.data(), buffer.size(),
            sector_id_string_fmt, static_cast<std::underlying_type_t<sector_id>>(id));

        buffer.pop_back(); // pop null terminator written by snprintf
        return buffer;
    }
}
