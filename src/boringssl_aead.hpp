#pragma once

#include <string>
#include <stdexcept>
#include <type_traits>

#include <openssl/base.h>
#if !__has_include(<openssl/is_boringssl.h>) || !defined OPENSSL_IS_BORINGSSL
#error "The aead abstraction uses boringssl specific APIs"
#endif

#include <openssl/err.h>
#include <openssl/cipher.h>
#include <openssl/aead.h>

#include <vefs/blob.hpp>
#include <vefs/exceptions.hpp>


namespace vefs::crypto::detail
{
    inline std::string read_openssl_errors(std::string str = std::string{})
    {
        auto printCb = [](const char *msg, size_t msgSize, void *ctx)
        {
            auto &str = *reinterpret_cast<std::string *>(ctx);
            str.append(msg, msgSize);
            str.push_back('\n');
            return 1;
        };

        if (!str.empty())
        {
            str.push_back('\n');
        }
        ERR_print_errors_cb(printCb, &str);
        if (!str.empty())
        {
            str.pop_back();
        }
        str.shrink_to_fit();
        return str;
    }

    enum class errinfo_openssl_tag {};
    using errinfo_openssl = boost::error_info<errinfo_openssl_tag, std::string>;

    inline auto make_openssl_errinfo(std::string desc = std::string{})
    {
        return errinfo_openssl{ read_openssl_errors(desc) };
    }

    class openssl_api_error
        : public virtual crypto_failure
    {
    };

    class aead_invalid_argument
        : public std::logic_error
    {
        using std::logic_error::logic_error;
    };

    class boringssl_aead
    {
    public:
        enum class scheme
        {
            aes_256_gcm,
        };

    private:
        static const EVP_AEAD * to_impl(scheme algorithm)
        {
            switch (algorithm)
            {
            case scheme::aes_256_gcm:
                return EVP_aead_aes_256_gcm();

            default:
                BOOST_THROW_EXCEPTION(aead_invalid_argument{ "unknown aead scheme value" });
            }
        }

    public:
        boringssl_aead(blob_view key, scheme algorithm = scheme::aes_256_gcm)
        {
            ERR_clear_error();

            const EVP_AEAD *impl = to_impl(algorithm);

            if (key.size() != EVP_AEAD_key_length(impl))
            {
                BOOST_THROW_EXCEPTION(aead_invalid_argument("invalid key size"));
            }

            if (!EVP_AEAD_CTX_init(&mCtx, impl,
                    reinterpret_cast<const uint8_t *>(key.data()), key.size(),
                    EVP_AEAD_DEFAULT_TAG_LENGTH, nullptr))
            {
                BOOST_THROW_EXCEPTION(openssl_api_error{}
                    << errinfo_api_function{ "EVP_AEAD_CTX_init" }
                    << make_openssl_errinfo());
            }
        }
        ~boringssl_aead()
        {
            ERR_clear_error();
            EVP_AEAD_CTX_cleanup(&mCtx);
        }

        std::size_t max_overhead() const
        {
            return EVP_AEAD_max_overhead(EVP_AEAD_CTX_aead(&mCtx));
        }
        static std::size_t max_overhead(scheme algorithm)
        {
            return EVP_AEAD_max_overhead(to_impl(algorithm));
        }
        std::size_t nonce_size() const
        {
            return EVP_AEAD_nonce_length(EVP_AEAD_CTX_aead(&mCtx));
        }
        static std::size_t nonce_size(scheme algorithm)
        {
            return EVP_AEAD_nonce_length(to_impl(algorithm));
        }

        void seal(blob out, blob &outTag, blob_view nonce, blob_view plain, blob_view ad = blob_view{}) const
        {
            if (!out)
            {
                BOOST_THROW_EXCEPTION(aead_invalid_argument{ "seal(): no ciphertext output buffer was supplied" });
            }
            if (!outTag)
            {
                BOOST_THROW_EXCEPTION(aead_invalid_argument{ "seal(): no tag output buffer was supplied" });
            }
            if (!nonce)
            {
                BOOST_THROW_EXCEPTION(aead_invalid_argument{ "seal(): no nonce was supplied" });
            }
            if (!plain)
            {
                BOOST_THROW_EXCEPTION(aead_invalid_argument{ "seal(): no plaintext was supplied" });
            }
            if (!ad)
            {
                // ensure ad.data() returns nullptr
                ad = blob_view{};
            }
            // extended parameter validation is done by openssl and are reported
            // through the resulting openssl_error

            size_t outTagLen = outTag.size();

            ERR_clear_error();
            if (!EVP_AEAD_CTX_seal_scatter(&mCtx,
                reinterpret_cast<uint8_t *>(out.data()),
                reinterpret_cast<uint8_t *>(outTag.data()), &outTagLen, outTag.size(),
                reinterpret_cast<const uint8_t *>(nonce.data()), nonce.size(),
                reinterpret_cast<const uint8_t *>(plain.data()), plain.size(),
                nullptr, 0, // extra_in, extra_in_len
                reinterpret_cast<const uint8_t *>(ad.data()), ad.size()
                ))
            {
                BOOST_THROW_EXCEPTION(openssl_api_error{}
                    << errinfo_api_function{ "EVP_AEAD_CTX_seal_scatter" }
                    << make_openssl_errinfo());
            }

            outTag = outTag.slice(0, outTagLen);
        }

        bool open(blob out, blob_view nonce, blob_view ciphertext, blob_view authTag, blob_view ad = blob_view{})
        {
            if (!out)
            {
                BOOST_THROW_EXCEPTION(aead_invalid_argument{ "seal(): no plaintext output buffer was supplied" });
            }
            if (!nonce)
            {
                BOOST_THROW_EXCEPTION(aead_invalid_argument{ "seal(): no nonce was supplied" });
            }
            if (!ciphertext)
            {
                BOOST_THROW_EXCEPTION(aead_invalid_argument{ "seal(): no ciphertext was supplied" });
            }
            if (!authTag)
            {
                BOOST_THROW_EXCEPTION(aead_invalid_argument{ "seal(): no authentication tag buffer was supplied" });
            }
            if (!ad)
            {
                // ensure ad.data() returns nullptr
                ad = blob_view{};
            }

            ERR_clear_error();
            if (!EVP_AEAD_CTX_open_gather(&mCtx,
                    reinterpret_cast<uint8_t *>(out.data()),
                    reinterpret_cast<const uint8_t *>(nonce.data()), nonce.size(),
                    reinterpret_cast<const uint8_t *>(ciphertext.data()), ciphertext.size(),
                    reinterpret_cast<const uint8_t *>(authTag.data()), authTag.size(),
                    reinterpret_cast<const uint8_t *>(ad.data()), ad.size()
                ))
            {
                auto ec = ERR_peek_last_error();
                if (!ec || (ERR_GET_LIB(ec) == ERR_LIB_CIPHER && ERR_GET_REASON(ec) == CIPHER_R_BAD_DECRYPT))
                {
                    ERR_clear_error();
                    // parameters etc. were formally correct, but the message is _bad_
                    return false;
                }
                BOOST_THROW_EXCEPTION(openssl_api_error{}
                    << errinfo_api_function{ "failed to open a msg for an unexpected reason" }
                    << make_openssl_errinfo());
            }
            return true;
        }

    private:
        EVP_AEAD_CTX mCtx;
    };
}
