#include "precompiled.hpp"
#include <vefs/crypto/provider.hpp>

#include <stdexcept>

#include "crypto_provider_boringssl.hpp"
#include "crypto_provider_debug.hpp"

namespace vefs::crypto
{
    namespace
    {
        detail::boringssl_aes_256_gcm_provider boringssl_aes_256_gcm;
        bool debug_provider_enabled = false;
    }

    void detail::enable_debug_provider()
    {
        debug_provider_enabled = true;
    }

    crypto_provider * boringssl_aes_256_gcm_crypto_provider()
    {
        return &boringssl_aes_256_gcm;
    }

    crypto_provider* debug_crypto_provider()
    {
        if (!debug_provider_enabled)
        {
            throw std::logic_error("The debug provider wasn't enabled");
        }
        static detail::debug_crypto_provider debug_provider;
        return &debug_provider;
    }
}
