#include "counter.hpp"

#include <boost/container/static_vector.hpp>
#include <dplx/cncr/math_supplement.hpp>
#include <dplx/scope_guard.hpp>

#include <dplx/dp/api.hpp>
#include <dplx/dp/cpos/container.std.hpp>
#include <dplx/dp/items/emit_core.hpp>
#include <dplx/dp/items/parse_ranges.hpp>

#include <vefs/platform/secure_memzero.hpp>

auto dplx::dp::codec<vefs::crypto::counter>::decode(parse_context &ctx,
                                                    value_type &value) noexcept
        -> result<void>
{
    boost::container::static_vector<std::byte, value_type::state_size> state{};
    dplx::scope_guard cleanupState = [&state] {
        vefs::utils::secure_memzero(state);
    };

    DPLX_TRY(dp::parse_binary_finite(ctx, state, value_type::state_size));
    if (state.size() != value_type::state_size)
    {
        return dp::errc::item_value_out_of_range;
    }
    value = value_type{vefs::ro_blob<value_type::state_size>{state}};
    return oc::success();
}
auto dplx::dp::codec<vefs::crypto::counter>::size_of(
        emit_context &ctx, value_type const &) noexcept -> std::uint64_t
{
    return dp::item_size_of_binary(ctx, value_type::state_size);
}
auto dplx::dp::codec<vefs::crypto::counter>::encode(
        emit_context &ctx, value_type const &value) noexcept -> result<void>
{
    auto &&state = value.view();
    return dp::emit_binary(ctx, state.data(), state.size());
}

auto dplx::dp::codec<vefs::crypto::atomic_counter>::decode(
        parse_context &ctx, value_type &value) noexcept -> result<void>
{
    DPLX_TRY(value, dp::decode(as_value<value_type::value_type>, ctx));
    return oc::success();
}
auto dplx::dp::codec<vefs::crypto::atomic_counter>::size_of(
        emit_context &ctx, value_type const &) noexcept -> std::uint64_t
{
    return dp::encoded_size_of(ctx, value_type::value_type{});
}
auto dplx::dp::codec<vefs::crypto::atomic_counter>::encode(
        emit_context &ctx, value_type const &value) noexcept -> result<void>
{
    return dp::encode(ctx, value.load());
}
