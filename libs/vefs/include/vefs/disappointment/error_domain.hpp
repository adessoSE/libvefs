#pragma once

#include <cstdint>

#include <memory>
#include <type_traits>
#include <string_view>

#include <vefs/disappointment/fwd.hpp>

namespace vefs
{
    class error_domain
    {
    public:
        error_domain(const error_domain &) = delete;
        error_domain & operator=(const error_domain &) = delete;

        virtual auto name() const noexcept
            -> std::string_view = 0;
        virtual auto message(const error &e, const error_code code) const noexcept
            -> std::string_view = 0;

        auto message(error &&e, const error_code code) const noexcept
            -> std::string_view = delete;

    protected:
        constexpr error_domain() noexcept = default;
        ~error_domain() = default;
    };
    static_assert(!std::is_copy_constructible_v<error_domain>);
    static_assert(!std::is_move_constructible_v<error_domain>);
    static_assert(!std::is_copy_assignable_v<error_domain>);
    static_assert(!std::is_move_assignable_v<error_domain>);

    constexpr bool operator==(const error_domain &lhs, const error_domain &rhs) noexcept
    {
        return &lhs == &rhs;
    }
    constexpr bool operator!=(const error_domain &lhs, const error_domain &rhs) noexcept
    {
        return &lhs != &rhs;
    }
    constexpr bool operator<(const error_domain &lhs, const error_domain &rhs) noexcept
    {
        return &lhs < &rhs;
    }
}
