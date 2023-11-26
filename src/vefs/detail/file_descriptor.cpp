#include "vefs/detail/file_descriptor.hpp"

#include <dplx/dp/codecs/auto_enum.hpp>
#include <dplx/dp/codecs/auto_object.hpp>
#include <dplx/dp/codecs/core.hpp>
#include <dplx/dp/codecs/std-container.hpp>
#include <dplx/dp/codecs/std-string.hpp>

auto dplx::dp::codec<vefs::detail::file_descriptor>::decode(
        parse_context &ctx, value_type &value) noexcept -> result<void>
{
    return dp::decode_object(ctx, value);
}
auto dplx::dp::codec<vefs::detail::file_descriptor>::size_of(
        emit_context &ctx, value_type const &value) noexcept -> std::uint64_t
{
    return dp::size_of_object(ctx, value);
}
auto dplx::dp::codec<vefs::detail::file_descriptor>::encode(
        emit_context &ctx, value_type const &value) noexcept -> result<void>
{
    return dp::encode_object(ctx, value);
}
