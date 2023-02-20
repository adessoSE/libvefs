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
result<void> kdf_impl(rw_dynblob prk,
                      ro_dynblob inputKey,
                      std::span<ro_dynblob const> domainIt) noexcept;
}

result<void>
kdf(rw_dynblob prk, ro_dynblob inputKey, ro_dynblob domain) noexcept;

template <typename... DomainParts>
result<void>
kdf(rw_dynblob prk, ro_dynblob inputKey, DomainParts const &...parts)
{
    std::array<ro_dynblob, sizeof...(DomainParts)> lparts{
            as_bytes(std::span(parts))...};

    return detail::kdf_impl(prk, inputKey, lparts);
}
} // namespace vefs::crypto
