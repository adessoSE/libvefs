#pragma once

#include <cstddef>

#include <array>
#include <initializer_list>
#include <type_traits>

#include <vefs/disappointment.hpp>
#include <vefs/span.hpp>
#include <vefs/utils/secure_array.hpp>

namespace vefs::crypto
{
namespace detail
{
auto kdf_impl(rw_dynblob prk,
              ro_dynblob inputKey,
              std::span<ro_dynblob const> domainIt) noexcept -> result<void>;
}

auto kdf(rw_dynblob prk, ro_dynblob inputKey, ro_dynblob domain) noexcept
        -> result<void>;

template <typename... DomainParts>
auto kdf(rw_dynblob prk, ro_dynblob inputKey, DomainParts const &...parts)
        -> result<void>
{
    std::array<ro_dynblob, sizeof...(DomainParts)> lparts{
            as_bytes(std::span(parts))...};

    return detail::kdf_impl(prk, inputKey, lparts);
}
} // namespace vefs::crypto
