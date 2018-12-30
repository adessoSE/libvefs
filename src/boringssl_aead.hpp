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
#include <vefs/disappointment.hpp>

namespace vefs::ed
{
    struct openssl_error_tag {};
    using openssl_error = error_detail<openssl_error_tag, std::string>;
}

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

    inline auto make_openssl_errinfo(std::string desc = std::string{})
    {
        return ed::openssl_error{ read_openssl_errors(desc) };
    }

    class openssl_api_error
        : public virtual crypto_failure
    {
    };


    inline std::size_t max_overhead(const EVP_AEAD *impl)
    {
        return EVP_AEAD_max_overhead(impl);
    }
    inline std::size_t nonce_size(const EVP_AEAD *impl)
    {
        return EVP_AEAD_nonce_length(impl);
    }

    class boringssl_aead final
    {
        boringssl_aead() = default;

    public:
        boringssl_aead(const boringssl_aead &) = delete;
        boringssl_aead(boringssl_aead &&other) noexcept
            : mCtx{ other.mCtx }
        {
            other.mCtx.aead = nullptr;
            other.mCtx.aead_state = nullptr;
            other.mCtx.tag_len = 0;
        }
        boringssl_aead & operator=(const boringssl_aead &) = delete;
        boringssl_aead & operator=(boringssl_aead &&other) noexcept
        {
            if (mCtx.aead_state)
            {
                ERR_clear_error();
                EVP_AEAD_CTX_cleanup(&mCtx);
            }
            mCtx.aead = other.mCtx.aead;
            mCtx.aead_state = other.mCtx.aead_state;
            mCtx.tag_len = other.mCtx.tag_len;
            other.mCtx.aead = nullptr;
            other.mCtx.aead_state = nullptr;
            other.mCtx.tag_len = 0;
            return *this;
        }

        static result<boringssl_aead> create(blob_view key,
            const EVP_AEAD *algorithm = EVP_aead_aes_256_gcm()) noexcept
        {
            using namespace std::string_view_literals;

            ERR_clear_error();

            if (key.size() != EVP_AEAD_key_length(algorithm))
            {
                return errc::invalid_argument;
            }

            boringssl_aead ctx;
            if (!EVP_AEAD_CTX_init(&ctx.mCtx, algorithm,
                reinterpret_cast<const uint8_t *>(key.data()), key.size(),
                EVP_AEAD_DEFAULT_TAG_LENGTH, nullptr))
            {
                return errc::bad
                    << ed::error_code_api_origin{ "EVP_AEAD_CTX_init"sv }
                    << make_openssl_errinfo();
            }
            return std::move(ctx);
        }
        ~boringssl_aead()
        {
            if (mCtx.aead_state)
            {
                ERR_clear_error();
                EVP_AEAD_CTX_cleanup(&mCtx);
            }
        }


        result<void> seal(blob out, blob &outTag, blob_view nonce, blob_view plain, blob_view ad = blob_view{}) const
        {
            using namespace std::string_view_literals;

            // narrow contract violations
            if (!out)
            {
                BOOST_THROW_EXCEPTION(invalid_argument{}
                    << errinfo_param_name{ "out" }
                    << errinfo_param_misuse_description{ "no ciphertext output buffer was supplied" }
                );
            }
            if (!outTag)
            {
                BOOST_THROW_EXCEPTION(invalid_argument{}
                    << errinfo_param_name{ "outTag" }
                    << errinfo_param_misuse_description{ "no tag output buffer was supplied" }
                );
            }
            if (!nonce)
            {
                BOOST_THROW_EXCEPTION(invalid_argument{}
                    << errinfo_param_name{ "nonce" }
                    << errinfo_param_misuse_description{ "no nonce was supplied" }
                );
            }
            if (!plain)
            {
                BOOST_THROW_EXCEPTION(invalid_argument{}
                    << errinfo_param_name{ "plain" }
                    << errinfo_param_misuse_description{ "no plaintext was supplied" }
                );
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
                // #TODO appropriately wrap the boringssl packed error
                return error{ errc::bad }
                    << ed::error_code_api_origin{ "EVP_AEAD_CTX_seal_scatter"sv }
                    << make_openssl_errinfo();
            }

            outTag = outTag.slice(0, outTagLen);

            return outcome::success();
        }

        result<void> open(blob out, blob_view nonce, blob_view ciphertext, blob_view authTag, blob_view ad = blob_view{})
        {
            if (!out)
            {
                BOOST_THROW_EXCEPTION(invalid_argument{}
                    << errinfo_param_name{ "out" }
                    << errinfo_param_misuse_description{ "no ciphertext output buffer was supplied" }
                );
            }
            if (!nonce)
            {
                BOOST_THROW_EXCEPTION(invalid_argument{}
                    << errinfo_param_name{ "nonce" }
                    << errinfo_param_misuse_description{ "no nonce was supplied" }
                );
            }
            if (!ciphertext)
            {
                BOOST_THROW_EXCEPTION(invalid_argument{}
                    << errinfo_param_name{ "ciphertext" }
                    << errinfo_param_misuse_description{ "seal(): no ciphertext was supplied" }
                );
            }
            if (!authTag)
            {
                BOOST_THROW_EXCEPTION(invalid_argument{}
                    << errinfo_param_name{ "authTag" }
                    << errinfo_param_misuse_description{ "no authentication tag buffer was supplied" }
                );
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
                    return archive_errc::tag_mismatch;
                }
                return error{ errc::bad }
                    << ed::error_code_api_origin{ "EVP_AEAD_CTX_open_gather" }
                    << make_openssl_errinfo();
            }
            return outcome::success();
        }

        friend std::size_t max_overhead(const boringssl_aead &ctx)
        {
            return EVP_AEAD_max_overhead(EVP_AEAD_CTX_aead(&ctx.mCtx));
        }
        friend std::size_t nonce_size(const boringssl_aead &ctx)
        {
            return EVP_AEAD_nonce_length(EVP_AEAD_CTX_aead(&ctx.mCtx));
        }

    private:
        EVP_AEAD_CTX mCtx;
    };
}
