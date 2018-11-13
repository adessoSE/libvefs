#pragma once

#include <cstdint>

#include <type_traits>
#include <string_view>

namespace vefs
{
    using raw_error_value = std::uintptr_t;
    class error_info2;

    class error_domain2
    {
        friend class error_info2;

    public:
        error_domain2(const error_domain2 &) = delete;
        error_domain2 & operator=(const error_domain2 &) = delete;

        virtual auto name() const noexcept
            -> std::string_view = 0;
        virtual auto message(raw_error_value handle, bool verbose) const noexcept
            -> std::string_view = 0;

    protected:
        constexpr error_domain2() noexcept = default;
        ~error_domain2() = default;
    };
    static_assert(!std::is_copy_constructible_v<error_domain2>);
    static_assert(!std::is_move_constructible_v<error_domain2>);
    static_assert(!std::is_copy_assignable_v<error_domain2>);
    static_assert(!std::is_move_assignable_v<error_domain2>);
    static_assert(alignof(error_domain2) >= 4);

    class error_domain
    {
    public:
        error_domain(const error_domain &) = delete;
        error_domain & operator=(const error_domain &) = delete;

        virtual std::string_view name() const noexcept = 0;
        virtual std::string_view message(intptr_t value) const noexcept = 0;

    protected:
        constexpr error_domain() noexcept = default;
        ~error_domain() = default;
    };

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
