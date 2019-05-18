#include "precompiled.hpp"
#include <vefs/crypto/kdf.hpp>

#include "blake2.hpp"

namespace vefs::crypto
{
    namespace detail
    {
        result<void> kdf_impl(rw_dynblob prk, ro_dynblob inputKey, span<const ro_dynblob> domain) noexcept
        {
            blake2xb state{};

            BOOST_OUTCOME_TRY(state.init(prk.size(), inputKey, vefs_blake2b_personalization_view));
            for (auto &part : domain)
            {
                BOOST_OUTCOME_TRY(state.update(part));
            }
            return state.final(prk);
        }
    }

    result<void> kdf(rw_dynblob prk, ro_dynblob inputKey, ro_dynblob domain) noexcept
    {
        using namespace detail;

        blake2xb state{};

        BOOST_OUTCOME_TRY(state.init(prk.size(), inputKey, vefs_blake2b_personalization_view));
        BOOST_OUTCOME_TRY(state.update(domain));
        return state.final(prk);
    }
}
