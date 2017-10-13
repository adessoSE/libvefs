#include "precompiled.hpp"
#include <vefs/crypto/kdf.hpp>

#include "blake2.hpp"

namespace vefs::crypto
{
    namespace
    {
        template <typename T>
        void kdf_impl(blob_view key, T domain, blob outputPRK)
        {
            detail::blake2xb(outputPRK.size(), key, detail::vefs_blake2b_personalization_view)
                .update(domain)
                .final(outputPRK);
        }
    }

    void kdf(blob_view key, const std::vector<blob_view> &domain, blob outputPRK)
    {
        kdf_impl(key, domain, outputPRK);
    }

    void kdf(blob_view key, std::initializer_list<blob_view> domain, blob outputPRK)
    {
        kdf_impl(key, domain, outputPRK);
    }

    void kdf(blob_view key, blob_view domain, blob outputPRK)
    {
        kdf_impl(key, domain, outputPRK);
    }
}
