#pragma once

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include <openssl/base.h>
#if !__has_include(<openssl/is_boringssl.h>) || !defined OPENSSL_IS_BORINGSSL
#error "The aead abstraction uses boringssl specific APIs"
#endif

#include <openssl/aead.h>
#include <openssl/cipher.h>
#include <openssl/err.h>

#include <vefs/disappointment.hpp>
#include <vefs/exceptions.hpp>
#include <vefs/span.hpp>

namespace vefs::ed
{
struct openssl_error_tag
{
};
using openssl_error = error_detail<openssl_error_tag, std::string>;
} // namespace vefs::ed

namespace vefs::crypto::detail
{
inline auto read_openssl_errors(std::string str = std::string{}) -> std::string
{
    auto printCb = [](char const *msg, size_t msgSize, void *ctx) {
        auto &cbStr = *reinterpret_cast<std::string *>(ctx);
        cbStr.append(msg, msgSize);
        cbStr.push_back('\n');
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
    return ed::openssl_error{read_openssl_errors(desc)};
}

inline auto max_overhead(const EVP_AEAD *impl) -> std::size_t
{
    return EVP_AEAD_max_overhead(impl);
}
inline auto nonce_size(const EVP_AEAD *impl) -> std::size_t
{
    return EVP_AEAD_nonce_length(impl);
}

class boringssl_aead final
{
    boringssl_aead() = default;

public:
    boringssl_aead(boringssl_aead const &) = delete;
    boringssl_aead(boringssl_aead &&other) noexcept
        : mCtx{other.mCtx}
        , mInitialized{other.mInitialized}
    {
        memset(&other.mCtx, 0, sizeof(other.mCtx));
    }
    auto operator=(boringssl_aead const &) -> boringssl_aead & = delete;
    auto operator=(boringssl_aead &&other) noexcept -> boringssl_aead &
    {
        if (mInitialized)
        {
            ERR_clear_error();
            EVP_AEAD_CTX_cleanup(&mCtx);
        }
        memcpy(&mCtx, &other.mCtx, sizeof(mCtx));
        memset(&other.mCtx, 0, sizeof(other.mCtx));
        mInitialized = std::exchange(other.mInitialized, false);
        return *this;
    }

    static auto create(ro_dynblob key,
                       const EVP_AEAD *algorithm
                       = EVP_aead_aes_256_gcm()) noexcept
            -> result<boringssl_aead>
    {
        using namespace std::string_view_literals;

        ERR_clear_error();

        if (key.size() != EVP_AEAD_key_length(algorithm))
        {
            return errc::invalid_argument;
        }

        boringssl_aead ctx;
        if (!EVP_AEAD_CTX_init(&ctx.mCtx, algorithm,
                               reinterpret_cast<uint8_t const *>(key.data()),
                               key.size(), EVP_AEAD_DEFAULT_TAG_LENGTH,
                               nullptr))
        {
            return archive_errc::bad
                   << ed::error_code_api_origin{"EVP_AEAD_CTX_init"sv}
                   << make_openssl_errinfo();
        }
        ctx.mInitialized = true;
        return ctx;
    }
    ~boringssl_aead()
    {
        if (mInitialized)
        {
            ERR_clear_error();
            EVP_AEAD_CTX_cleanup(&mCtx);
        }
    }

    auto seal(rw_dynblob out,
              rw_dynblob &outTag,
              ro_dynblob nonce,
              ro_dynblob plain,
              ro_dynblob ad = ro_dynblob{}) const -> result<void>
    {
        using namespace std::string_view_literals;

        // narrow contract violations
        if (out.empty())
        {
            BOOST_THROW_EXCEPTION(
                    invalid_argument{}
                    << errinfo_param_name{"out"}
                    << errinfo_param_misuse_description{
                               "no ciphertext output buffer was supplied"});
        }
        if (outTag.empty())
        {
            BOOST_THROW_EXCEPTION(
                    invalid_argument{}
                    << errinfo_param_name{"outTag"}
                    << errinfo_param_misuse_description{
                               "no tag output buffer was supplied"});
        }
        if (nonce.empty())
        {
            BOOST_THROW_EXCEPTION(invalid_argument{}
                                  << errinfo_param_name{"nonce"}
                                  << errinfo_param_misuse_description{
                                             "no nonce was supplied"});
        }
        if (plain.empty())
        {
            BOOST_THROW_EXCEPTION(invalid_argument{}
                                  << errinfo_param_name{"plain"}
                                  << errinfo_param_misuse_description{
                                             "no plaintext was supplied"});
        }
        // extended parameter validation is done by openssl and are reported
        // through the resulting openssl_error

        size_t outTagLen = outTag.size();

        ERR_clear_error();
        if (!EVP_AEAD_CTX_seal_scatter(
                    &mCtx, reinterpret_cast<uint8_t *>(out.data()),
                    reinterpret_cast<uint8_t *>(outTag.data()), &outTagLen,
                    outTag.size(),
                    reinterpret_cast<uint8_t const *>(nonce.data()),
                    nonce.size(),
                    reinterpret_cast<uint8_t const *>(plain.data()),
                    plain.size(), nullptr,
                    0, // extra_in, extra_in_len
                    reinterpret_cast<uint8_t const *>(ad.data()), ad.size()))
        {
            // #TODO appropriately wrap the boringssl packed error
            return archive_errc::bad
                   << ed::error_code_api_origin{"EVP_AEAD_CTX_seal_scatter"sv}
                   << make_openssl_errinfo();
        }

        outTag = outTag.subspan(0, outTagLen);

        return outcome::success();
    }

    auto open(rw_dynblob out,
              ro_dynblob nonce,
              ro_dynblob ciphertext,
              ro_dynblob authTag,
              ro_dynblob ad = ro_dynblob{}) -> result<void>
    {
        if (out.empty())
        {
            BOOST_THROW_EXCEPTION(
                    invalid_argument{}
                    << errinfo_param_name{"out"}
                    << errinfo_param_misuse_description{
                               "no ciphertext output buffer was supplied"});
        }
        if (nonce.empty())
        {
            BOOST_THROW_EXCEPTION(invalid_argument{}
                                  << errinfo_param_name{"nonce"}
                                  << errinfo_param_misuse_description{
                                             "no nonce was supplied"});
        }
        if (ciphertext.empty())
        {
            BOOST_THROW_EXCEPTION(
                    invalid_argument{}
                    << errinfo_param_name{"ciphertext"}
                    << errinfo_param_misuse_description{
                               "seal(): no ciphertext was supplied"});
        }
        if (authTag.empty())
        {
            BOOST_THROW_EXCEPTION(
                    invalid_argument{}
                    << errinfo_param_name{"authTag"}
                    << errinfo_param_misuse_description{
                               "no authentication tag buffer was supplied"});
        }

        ERR_clear_error();
        if (!EVP_AEAD_CTX_open_gather(
                    &mCtx, reinterpret_cast<uint8_t *>(out.data()),
                    reinterpret_cast<uint8_t const *>(nonce.data()),
                    nonce.size(),
                    reinterpret_cast<uint8_t const *>(ciphertext.data()),
                    ciphertext.size(),
                    reinterpret_cast<uint8_t const *>(authTag.data()),
                    authTag.size(),
                    reinterpret_cast<uint8_t const *>(ad.data()), ad.size()))
        {
            auto ec = ERR_peek_last_error();
            if (!ec
                || (ERR_GET_LIB(ec) == ERR_LIB_CIPHER
                    && ERR_GET_REASON(ec) == CIPHER_R_BAD_DECRYPT))
            {
                ERR_clear_error();
                // parameters etc. were formally correct, but the message is
                // _bad_
                return archive_errc::tag_mismatch;
            }
            return archive_errc::bad
                   << ed::error_code_api_origin{"EVP_AEAD_CTX_open_gather"}
                   << make_openssl_errinfo();
        }
        return outcome::success();
    }

    friend auto max_overhead(boringssl_aead const &ctx) -> std::size_t
    {
        return EVP_AEAD_max_overhead(EVP_AEAD_CTX_aead(&ctx.mCtx));
    }
    friend auto nonce_size(boringssl_aead const &ctx) -> std::size_t
    {
        return EVP_AEAD_nonce_length(EVP_AEAD_CTX_aead(&ctx.mCtx));
    }

private:
    EVP_AEAD_CTX mCtx;
    bool mInitialized{false};
};
} // namespace vefs::crypto::detail
