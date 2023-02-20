#pragma once

#include <cstdint>

#include <memory>
#include <string_view>
#include <type_traits>

#include <vefs/disappointment/fwd.hpp>

namespace vefs
{
class error_domain
{
public:
    error_domain(error_domain const &) = delete;
    error_domain &operator=(error_domain const &) = delete;

    virtual auto name() const noexcept -> std::string_view = 0;
    virtual auto message(error const &e, const error_code code) const noexcept
            -> std::string_view
            = 0;

    auto message(error &&e, const error_code code) const noexcept
            -> std::string_view
            = delete;

protected:
    constexpr error_domain() noexcept = default;
    ~error_domain() = default;
};
static_assert(!std::is_copy_constructible_v<error_domain>);
static_assert(!std::is_move_constructible_v<error_domain>);
static_assert(!std::is_copy_assignable_v<error_domain>);
static_assert(!std::is_move_assignable_v<error_domain>);

constexpr bool operator==(error_domain const &lhs,
                          error_domain const &rhs) noexcept
{
    return &lhs == &rhs;
}
constexpr bool operator!=(error_domain const &lhs,
                          error_domain const &rhs) noexcept
{
    return &lhs != &rhs;
}
constexpr bool operator<(error_domain const &lhs,
                         error_domain const &rhs) noexcept
{
    return &lhs < &rhs;
}
} // namespace vefs
