#include "kdf.hpp"

#include "blake2.hpp"

namespace vefs::crypto
{

namespace detail
{

result<void> kdf_impl(rw_dynblob prk,
                      ro_dynblob inputKey,
                      std::span<ro_dynblob const> domain) noexcept
{
    blake2xb state{};

    VEFS_TRY(state.init(prk.size(), inputKey,
                        vefs_blake2b_personalization_view));
    for (auto &part : domain)
    {
        VEFS_TRY(state.update(part));
    }
    return state.final(prk);
}

} // namespace detail

result<void>
kdf(rw_dynblob prk, ro_dynblob inputKey, ro_dynblob domain) noexcept
{
    using namespace detail;

    blake2xb state{};

    VEFS_TRY(state.init(prk.size(), inputKey,
                        vefs_blake2b_personalization_view));
    VEFS_TRY(state.update(domain));
    return state.final(prk);
}

} // namespace vefs::crypto
