#include "vefs/detail/archive_header.hpp"

#include <dplx/dp/codecs/auto_object.hpp>
#include <dplx/dp/codecs/core.hpp>
#include <dplx/dp/codecs/std-container.hpp>

auto dplx::dp::codec<vefs::detail::archive_header>::decode(
        parse_context &ctx, value_type &value) noexcept -> result<void>
{
    return dp::decode_object(ctx, value);
}
auto dplx::dp::codec<vefs::detail::archive_header>::size_of(
        emit_context &ctx, value_type const &value) noexcept -> std::uint64_t
{
    return dp::size_of_object(ctx, value);
}
auto dplx::dp::codec<vefs::detail::archive_header>::encode(
        emit_context &ctx, value_type const &value) noexcept -> result<void>
{
    return dp::encode_object(ctx, value);
}
