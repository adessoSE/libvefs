#pragma once

#include <cstddef>

#include <array>
#include <type_traits>
#include <initializer_list>

#include <vefs/blob.hpp>
#include <vefs/disappointment.hpp>
#include <vefs/utils/secure_array.hpp>

namespace vefs::crypto
{
    namespace detal
    {
        result<void> kdf_impl(blob prk, blob_view inputKey, blob_view *domainIt, blob_view *domainEnd) noexcept;
    }

    result<void> kdf(blob prk, blob_view inputKey, blob_view domain) noexcept;

    template <typename... DomainParts>
    result<void> kdf(blob prk, blob_view inputKey, const DomainParts &... parts)
    {
        std::array<blob_view, sizeof...(DomainParts)> lparts{ blob_view(parts)... };
        auto begin = lparts.data();
        auto end = begin + lparts.size();

        return detal::kdf_impl(prk, inputKey, begin, end);
    }
}

