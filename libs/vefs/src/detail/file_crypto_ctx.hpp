#pragma once

#include <cstddef>
#include <mutex>

#include <vefs/disappointment.hpp>
#include <vefs/span.hpp>
#include <vefs/utils/secure_array.hpp>

#include "../crypto/counter.hpp"
#include "../crypto/provider.hpp"

namespace vefs::detail
{
class file_crypto_ctx_interface
{
public:
    virtual auto seal_sector(rw_blob<1 << 15> ciphertext,
                             rw_blob<16> mac,
                             crypto::crypto_provider &provider,
                             ro_blob<16> sessionSalt,
                             ro_blob<(1 << 15) - (1 << 5)> data) noexcept
            -> result<void> = 0;
    virtual auto unseal_sector(rw_blob<(1 << 15) - (1 << 5)> data,
                               crypto::crypto_provider &provider,
                               ro_blob<1 << 15> ciphertext,
                               ro_blob<16> mac) const noexcept
            -> result<void> = 0;
};
class file_crypto_ctx
{
public:
    struct state_type
    {
        utils::secure_byte_array<32> secret;
        crypto::counter counter;
    };

    enum class zero_init_t
    {
    };
    static inline constexpr zero_init_t zero_init = zero_init_t{};

    file_crypto_ctx(zero_init_t);
    file_crypto_ctx(ro_blob<32> secretView, crypto::counter secretCounter);
    explicit file_crypto_ctx(state_type const &state)
        : mState(state)
        , mStateSync()
    {
    }

    auto state() const noexcept -> state_type;

    // #TODO extract sector_device constants
    auto seal_sector(rw_blob<1 << 15> ciphertext,
                     rw_blob<16> mac,
                     crypto::crypto_provider &provider,
                     ro_blob<16> sessionSalt,
                     ro_blob<(1 << 15) - (1 << 5)> data) noexcept
            -> result<void>;
    auto unseal_sector(rw_blob<(1 << 15) - (1 << 5)> data,
                       crypto::crypto_provider &provider,
                       ro_blob<1 << 15> ciphertext,
                       ro_blob<16> mac) const noexcept -> result<void>;

private:
    state_type mState;
    mutable std::mutex mStateSync;
};

inline auto detail::file_crypto_ctx::state() const noexcept -> state_type
{
    std::lock_guard lock(mStateSync);
    return mState;
}

} // namespace vefs::detail
