#pragma once

#include <cstdint>

#include <string_view>

namespace vefs
{

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
