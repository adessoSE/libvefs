#include "precompiled.hpp"
#include <vefs/crypto/kdf.hpp>

#include "blake2.hpp"

namespace vefs::crypto
{
    namespace detail
    {
        result<void> kdf_impl(blob prk, blob_view inputKey, blob_view *domainIt, blob_view *domainEnd) noexcept
        {
            blake2xb state{};

            OUTCOME_TRY(state.init(prk.size(), inputKey, vefs_blake2b_personalization_view));
            for (; domainIt != domainEnd; ++domainIt)
            {
                OUTCOME_TRY(state.update(*domainIt));
            }
            return state.final(prk);
        }
    }

    result<void> kdf(blob prk, blob_view inputKey, blob_view domain) noexcept
    {
        using namespace detail;

        blake2xb state{};

        OUTCOME_TRY(state.init(prk.size(), inputKey, vefs_blake2b_personalization_view));
        OUTCOME_TRY(state.update(domain));
        return state.final(prk);
    }
}
