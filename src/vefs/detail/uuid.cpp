#include <vefs/utils/uuid.hpp>

#include <array>
#include <bit>

#include <dplx/dp/items/emit_ranges.hpp>
#include <dplx/dp/items/parse_ranges.hpp>

auto dplx::dp::codec<vefs::uuid>::decode(parse_context &ctx,
                                         uuid &value) noexcept -> result<void>
{
    DPLX_TRY(dp::expect_item_head(ctx, type_code::binary, sizeof(uuid)));
    if (ctx.in.size() < sizeof(uuid))
    {
        DPLX_TRY(ctx.in.require_input(sizeof(uuid)));
    }
    std::memcpy(&value, ctx.in.data(), sizeof(uuid));
    ctx.in.discard_buffered(sizeof(uuid));
    return oc::success();
}

auto dplx::dp::codec<vefs::uuid>::encode(emit_context &ctx, uuid value) noexcept
        -> result<void>
{
    auto raw = std::bit_cast<std::array<std::byte, sizeof(uuid)>>(value);
    return dp::emit_binary(ctx, raw.data(), sizeof(uuid));
}
