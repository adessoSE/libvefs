#pragma once

#include <cstdint>
#include <cstdio>

#include <iomanip>
#include <limits>
#include <ostream>
#include <string>

#include <boost/io/ios_state.hpp>
#include <fmt/ostream.h>

#include <vefs/disappointment/error_detail.hpp>

namespace vefs::detail
{
    enum class sector_id : std::uint64_t
    {
        master = 0,
    };

    inline bool operator<(sector_id lhs, sector_id rhs)
    {
        return static_cast<std::uint64_t>(lhs) <
               static_cast<std::uint64_t>(rhs);
    }
} // namespace vefs::detail

namespace fmt
{
    template <>
    struct formatter<vefs::detail::sector_id>
    {
        template <typename ParseContext>
        constexpr auto parse(ParseContext &ctx)
        {
            return ctx.begin();
        }

        template <typename FormatContext>
        auto format(const vefs::detail::sector_id id, FormatContext &ctx)
        {
            using itype = std::underlying_type_t<vefs::detail::sector_id>;
            return format_to(ctx.out(), "SIDX:{:04x}", static_cast<itype>(id));
        }
    };
} // namespace fmt

namespace vefs::detail
{
    template <typename CharT, typename Traits>
    inline auto operator<<(std::basic_ostream<CharT, Traits> &ostr,
                           sector_id sid) -> std::basic_ostream<CharT, Traits> &
    {
        fmt::print(ostr, "{}", sid);
        return ostr;
    }
} // namespace vefs::detail

namespace vefs::ed
{
    enum class sector_idx_tag
    {
    };
    using sector_idx = error_detail<sector_idx_tag, detail::sector_id>;
} // namespace vefs::ed
