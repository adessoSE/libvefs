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

    /*
    inline bool operator==(sector_id lhs, sector_id rhs)
    {
        return static_cast<std::uint64_t>(lhs) == static_cast<std::uint64_t>(rhs);
    }
    inline bool operator!=(sector_id lhs, sector_id rhs)
    {
        return !(lhs == rhs);
    }
    */

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

    inline std::string to_string(sector_id id)
    {
        using raw_type = std::underlying_type_t<sector_id>;
        using limits = std::numeric_limits<raw_type>;
        constexpr auto hexDigits = limits::digits >> 2;
        static_assert(hexDigits == 16);

        std::string buffer(hexDigits + 3, '\0');
        snprintf(buffer.data(), buffer.size(), "0x%.16llx", static_cast<raw_type>(id));
        buffer.pop_back(); // pop null terminator written by snprintf
        return buffer;
    }
}
