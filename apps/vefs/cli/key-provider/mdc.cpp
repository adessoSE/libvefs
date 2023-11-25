#include "vefs/cli/key-provider/mdc.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <boost/algorithm/hex.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/predef/os.h>
#include <dplx/cncr/windows-proper.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <status-code/boost_error_code.hpp>

#include <vefs/archive.hpp>

#include "../../../../src/vefs/crypto/boringssl_aead.hpp"
#include "vefs/cli/commandlets/base.hpp"
#include "vefs/cli/utils.hpp"

#if defined(DPLX_COMP_GNUC_AVAILABLE) && !defined(DPLX_COMP_GNUC_EMULATED)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
#include <boost/json/src.hpp>
#if defined(DPLX_COMP_GNUC_AVAILABLE) && !defined(DPLX_COMP_GNUC_EMULATED)
#pragma GCC diagnostic pop
#endif

namespace vefs
{

class boringssl_sha256
{
    ::SHA256_CTX mCtx;

public:
    boringssl_sha256() noexcept
    {
        if (auto errCode = ::SHA256_Init(&mCtx); errCode != 1)
        {
            std::abort();
        }
    }

    using digest_type = std::array<std::byte, 32>;

    void update(std::byte const *data, std::size_t size) noexcept
    {
        if (auto errCode = ::SHA256_Update(&mCtx, data, size); errCode != 1)
        {
            std::abort();
        }
    }
    auto final() noexcept -> digest_type
    {
        digest_type hash;
        if (auto errCode
            = ::SHA256_Final(reinterpret_cast<uint8_t *>(hash.data()), &mCtx);
            errCode != 1)
        {
            std::abort();
        }
        return hash;
    }
};

} // namespace vefs

namespace vefs::cli
{

constexpr std::string_view CRYPT_USER_ID = "user";
constexpr std::string_view CRYPT_MACHINE_ID = "machine";
constexpr std::string_view CRYPT_KEY = "key";
constexpr std::string_view CRYPT_SALT = "pbkdf2-salt";
constexpr std::string_view CRYPT_ENC = "enc";
constexpr std::string_view CRYPT_TAG = "tag";
constexpr std::string_view CRYPT_TYPE = "type";
constexpr std::string_view CRYPT_ITERATIONS = "pbkdf2-iterations";

struct mdc_password_encrypted_key_box
{
    std::vector<std::byte> ciphertext;
    std::vector<std::byte> auth_tag;
    std::vector<std::byte> pbkdf2_salt;
    unsigned pbkdf2_iterations;
};
auto mdc_retrieve_key_box(llfio::path_view archivePath)
        -> result<mdc_password_encrypted_key_box>;
auto mdc_derive_key_id(boost::json::object const &mdcBoxObject) noexcept
        -> result<std::array<std::byte, 64>>;

auto read_password_from_stdin() -> std::string;

auto mdc_derive_key(llfio::path_view archivePath, std::string_view password)
        -> result<storage_key>
{
    std::string passwordStorage;
    if (password.empty())
    {
        passwordStorage = read_password_from_stdin();
        password = passwordStorage;
    }

    VEFS_TRY(auto const keyBox, mdc_retrieve_key_box(archivePath));

    std::array<std::byte, 44> passwordKeyAndNonce{};
    if (auto const result = ::PKCS5_PBKDF2_HMAC(
                password.data(), password.size(),
                reinterpret_cast<uint8_t const *>(keyBox.pbkdf2_salt.data()),
                keyBox.pbkdf2_salt.size(), keyBox.pbkdf2_iterations,
                EVP_sha512(), passwordKeyAndNonce.size(),
                reinterpret_cast<std::uint8_t *>(passwordKeyAndNonce.data()));
        result != 1)
    {
        return errc::not_enough_memory;
    }

    // retrieving storageKey by decrypting enc with aes-256-gcm using
    // passwordKey, nonce and tag
    auto const keyBoxKey = std::span(passwordKeyAndNonce).first<32>();
    auto const keyBoxNonce = std::span(passwordKeyAndNonce).last<12>();

    VEFS_TRY(auto &&aead,
             vefs::crypto::detail::boringssl_aead::create(keyBoxKey));

    storage_key key{};
    if (auto boxOpenRx
        = aead.open(key.bytes, keyBoxNonce, keyBox.ciphertext, keyBox.auth_tag);
        boxOpenRx.has_failure())
    {
        if (boxOpenRx.error() == archive_errc::tag_mismatch)
        {
            return cli_errc::wrong_password;
        }
        return std::move(boxOpenRx).as_failure();
    }
    return key;
}

auto mdc_retrieve_key_box(llfio::path_view archivePath)
        -> result<mdc_password_encrypted_key_box>
{
    using namespace std::string_view_literals;

    std::array<std::byte, 1 << 12> readContent{};
    VEFS_TRY(vefs::read_archive_personalization_area({}, archivePath,
                                                     readContent));

    unsigned const jsonSize = boost::endian::load_big_u16(
            reinterpret_cast<unsigned char *>(readContent.data()));
    if (jsonSize > (1u << 12) - 2u)
    {
        return cli_errc::malformed_mdc_key_box;
    }

    boost::json::error_code jsonParseErrorCode;
    auto const jsonKeyBox = boost::json::parse(boost::json::string_view(
            reinterpret_cast<char const *>(readContent.data() + 2), jsonSize));
    if (jsonParseErrorCode)
    {
        return jsonParseErrorCode;
    }

    mdc_password_encrypted_key_box box;
    try
    {
        auto mdcBoxObject = jsonKeyBox.as_object();

        VEFS_TRY(auto const keyId, mdc_derive_key_id(mdcBoxObject));

        auto keyBoxObject = mdcBoxObject.at(CRYPT_KEY).as_object();

        auto keyType = keyBoxObject.at(CRYPT_TYPE).as_string();
        if (keyType == "password"sv)
        {
            auto int64Iterations = keyBoxObject.at(CRYPT_ITERATIONS).as_int64();
            if (!std::in_range<unsigned>(int64Iterations)
                || int64Iterations == 0)
            {
                return cli_errc::malformed_mdc_key_box;
            }
            box.pbkdf2_iterations = static_cast<unsigned>(int64Iterations);

            // derive passwordKey and nonce using keyID, userPW, pbkdf2-salt
            // and pbkdf2-iterations
            VEFS_TRY(box.pbkdf2_salt,
                     base64url_decode(keyBoxObject.at(CRYPT_SALT).as_string()));
            box.pbkdf2_salt.insert(box.pbkdf2_salt.end(), keyId.begin(),
                                   keyId.end());

            VEFS_TRY(box.ciphertext,
                     base64url_decode(keyBoxObject.at(CRYPT_ENC).as_string()));
            VEFS_TRY(box.auth_tag,
                     base64url_decode(keyBoxObject.at(CRYPT_TAG).as_string()));
        }
        else
        {
            return cli_errc::unsupported_mdc_key_type;
        }
    }
    catch (std::invalid_argument const &)
    {
        return cli_errc::malformed_mdc_key_box;
    }

    return box;
}

auto mdc_derive_key_id(boost::json::object const &mdcBoxObject) noexcept
        -> result<std::array<std::byte, 64>>
{
    boringssl_sha256 hashCtx;

    for (auto id : {mdcBoxObject.if_contains(CRYPT_USER_ID),
                    mdcBoxObject.if_contains(CRYPT_MACHINE_ID)})
    {
        if (id == nullptr)
        {
            return cli_errc::malformed_mdc_key_box;
        }
        if (auto idStr = id->if_string(); idStr == nullptr)
        {
            return cli_errc::malformed_mdc_key_box;
        }
        else
        {
            hashCtx.update(reinterpret_cast<std::byte const *>(idStr->data()),
                           idStr->size());
        }
    }
    auto keyId = hashCtx.final();

    std::array<std::byte, 64> hexKeyId;
    boost::algorithm::hex_lower(
            std::bit_cast<std::array<uint8_t, SHA256_DIGEST_LENGTH>>(keyId),
            reinterpret_cast<char *>(hexKeyId.data()));

    return hexKeyId;
}

// TODO refactor with ftxui and shouldn't reside in this file
auto read_password_from_stdin() -> std::string
{
    fmt::print("Please enter your password: ");

#if defined(BOOST_OS_WINDOWS_AVAILABLE)
    HANDLE hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;

    // Create a restore point Mode
    GetConsoleMode(hStdInput, &mode);
    // Enable echo input
    SetConsoleMode(hStdInput, mode & (~ENABLE_ECHO_INPUT));
#endif

    std::string input;
    std::getline(std::cin, input);
    fmt::print("\n");

    // Restore the mode
#if defined(BOOST_OS_WINDOWS_AVAILABLE)
    SetConsoleMode(hStdInput, mode);
#endif

    return input;
}

} // namespace vefs::cli
