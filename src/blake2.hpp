#pragma once

#include <array>
#include <stdexcept>

#include <boost/predef.h>

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(push)
#pragma warning(disable: 4804)
#endif

#include <blake2.h>

#if defined BOOST_COMP_MSVC_AVAILABLE
#pragma warning(pop)
#endif

#include <boost/preprocessor/stringize.hpp>

#include <vefs/blob.hpp>
#include <vefs/utils/secure_array.hpp>
#include <vefs/utils/misc.hpp>


namespace vefs::crypto::detail
{
    struct blake2_api_error
        : public std::logic_error
    {
        using std::logic_error::logic_error;
    };

    struct blake2_invalid_argument
        : public std::invalid_argument
    {
        using std::invalid_argument::invalid_argument;
    };

#define B2_ERRMSG(msg) (__FILE__ "[" BOOST_PP_STRINGIZE(__LINE__) "] " msg)

    template< typename D >
    class blake2_base
    {
        D * self()
        {
            return static_cast<D *>(this);
        }

    protected:
        template <typename T, typename... Args>
        static void safe_call(T api, const char *errMsg, Args... args)
        {
            if (api(std::forward<Args>(args)...))
            {
                throw blake2_api_error(errMsg);
            }
        }

        void init_key(blob_view key)
        {
            utils::secure_byte_array<D::block_bytes> keyBlockMemory;
            blob keyBlock{ keyBlockMemory };
            key.copy_to(keyBlock);
            if (auto remainingSpace = keyBlock.slice(key.size()))
            {
                fill_blob(remainingSpace);
            }

            self()->update(keyBlock);
        }

    public:
        D & update(blob data)
        {
            return self()->update(blob_view{ data });
        }

        template <typename Container>
        D & update(const Container &data)
        {
            for (const auto& bv : data)
            {
                self()->update(blob_view{ bv });
            }
            return *self();
        }

    protected:
        blake2_base() = default;
        blake2_base(const blake2_base &) = default;
        blake2_base(blake2_base &&) = default;
        blake2_base & operator=(const blake2_base &) = default;
        blake2_base & operator=(blake2_base &&) = default;
        ~blake2_base() = default;
    };

    class blake2b
        : public blake2_base<blake2b>
    {
        template <typename T, typename... Args>
        void ctx_safe_call(T api, const char *errMsg, Args... args)
        {
            safe_call(std::forward<T>(api), errMsg, &mState, std::forward<Args>(args)...);
        }

    public:
        static constexpr size_t salt_bytes = BLAKE2B_SALTBYTES;
        static constexpr size_t personal_bytes = BLAKE2B_PERSONALBYTES;
        static constexpr size_t digest_bytes = BLAKE2B_OUTBYTES;
        static constexpr size_t block_bytes = BLAKE2B_BLOCKBYTES;
        static constexpr size_t max_key_bytes = BLAKE2B_KEYBYTES;

        struct no_init_tag {};

        explicit blake2b(std::size_t digestSize = digest_bytes)
        {
            init(digestSize);
        }
        blake2b(std::size_t digestSize, blob_view key)
        {
            init(digestSize, key);
        }
        blake2b(std::size_t digestSize, blob_view key, blob_view personalisation)
        {
            init(digestSize, key, personalisation);
        }
        explicit blake2b(no_init_tag)
        {
        }
        ~blake2b()
        {
            utils::secure_data_erase(mState);
        }

        blake2b& init(std::size_t digestSize = digest_bytes)
        {
            if (!digestSize || digestSize < 16 || digestSize > block_bytes)
            {
                throw blake2_invalid_argument(B2_ERRMSG("requested an invalid digest size"));
            }

            ctx_safe_call(blake2b_init, B2_ERRMSG("blake2b_init failed"),
                digestSize);
            return *this;
        }
        blake2b& init(std::size_t digestSize, blob_view key)
        {
            if (!digestSize || digestSize < 16 || digestSize > block_bytes)
            {
                throw blake2_invalid_argument(B2_ERRMSG("requested an invalid digest size"));
            }
            if (!key || key.size() > max_key_bytes)
            {
                throw blake2_invalid_argument(B2_ERRMSG("provided an invalid blake2b key"));
            }

            ctx_safe_call(blake2b_init_key, B2_ERRMSG("blake2b_init_key failed"),
                digestSize, key.data(), key.size());
            return *this;
        }

        blake2b& init(std::size_t digestSize, blob_view key, blob_view personalisation)
        {
            if (!digestSize || digestSize < 16 || digestSize > block_bytes)
            {
                throw blake2_invalid_argument(B2_ERRMSG("requested an invalid digest size"));
            }
            if (!personalisation || personalisation.size() != sizeof(std::declval<blake2b_param>().personal))
            {
                throw blake2_invalid_argument(B2_ERRMSG("provided an invalid personalisation blob"));
            }
            if (key.size() > max_key_bytes)
            {
                throw blake2_invalid_argument(B2_ERRMSG("provided an invalid blake2b key"));
            }

            blake2b_param param;

            param.digest_length = static_cast<uint8_t>(digestSize);
            param.key_length = static_cast<uint8_t>(key.size());
            param.fanout = 1;
            param.depth = 1;
            param.leaf_length = 0;
            param.node_offset = 0;
            param.xof_length = 0;
            param.node_depth = 0;
            param.inner_length = 0;
            fill_blob(blob{ param.reserved });
            fill_blob(blob{ param.salt });
            personalisation.copy_to(blob{ param.personal });

            ctx_safe_call(blake2b_init_param, B2_ERRMSG("blake2b_init_param with personal failed"),
                &param);

            if (key)
            {
                init_key(key);
            }

            return *this;
        }

        using blake2_base::update;
        blake2b& update(blob_view data)
        {
            ctx_safe_call(blake2b_update, B2_ERRMSG("blake2b_update failed"),
                data.data(), data.size());
            return *this;
        }

        void final(blob digest)
        {
            ctx_safe_call(blake2b_final, B2_ERRMSG("blake2b_final failed"),
                digest.data(), digest.size());
        }

    private:
        blake2b_state mState;
    };

    class blake2xb
        : public blake2_base<blake2xb>
    {
        template <typename T, typename... Args>
        void ctx_safe_call(T api, const char *errMsg, Args... args)
        {
            safe_call(std::forward<T>(api), errMsg, &mState, std::forward<Args>(args)...);
        }

    public:
        static constexpr size_t salt_bytes = BLAKE2B_SALTBYTES;
        static constexpr size_t personal_bytes = BLAKE2B_PERSONALBYTES;
        static constexpr size_t block_bytes = BLAKE2B_BLOCKBYTES;
        static constexpr size_t max_key_bytes = BLAKE2B_KEYBYTES;
        static constexpr uint32_t variable_digest_length = 0xFFFF'FFFFu;

        struct no_init_tag {};

        explicit blake2xb(std::size_t digestSize)
        {
            init(digestSize);
        }
        blake2xb(std::size_t digestSize, blob_view key)
        {
            init(digestSize, key);
        }
        blake2xb(std::size_t digestSize, blob_view key, blob_view personalisation)
        {
            init(digestSize, key, personalisation);
        }
        explicit blake2xb(no_init_tag)
        {
        }
        ~blake2xb()
        {
            utils::secure_data_erase(mState);
        }

        blake2xb & init(std::size_t digestSize)
        {
            if (!digestSize || digestSize > variable_digest_length)
            {
                throw blake2_invalid_argument(B2_ERRMSG("requested an invalid digest size"));
            }

            ctx_safe_call(blake2xb_init, B2_ERRMSG("blake2xb_init failed"),
                digestSize);
            return *this;
        }
        blake2xb & init(std::size_t digestSize, blob_view key)
        {
            if (!digestSize || digestSize > variable_digest_length)
            {
                throw blake2_invalid_argument(B2_ERRMSG("requested an invalid digest size"));
            }
            if (!key || key.size() > max_key_bytes)
            {
                throw blake2_invalid_argument(B2_ERRMSG("provided an invalid blake2b key"));
            }

            ctx_safe_call(blake2xb_init_key, B2_ERRMSG("blake2xb_init_key failed"),
                digestSize, key.data(), key.size());
            return *this;
        }
        blake2xb & init(std::size_t digestSize, blob_view key, blob_view personalisation)
        {
            if (!digestSize || digestSize > variable_digest_length)
            {
                throw blake2_invalid_argument(B2_ERRMSG("requested an invalid digest size"));
            }
            if (!personalisation || personalisation.size() != sizeof(std::declval<blake2b_param>().personal))
            {
                throw blake2_invalid_argument(B2_ERRMSG("provided an invalid personalisation blob"));
            }
            if (key.size() > max_key_bytes)
            {
                throw blake2_invalid_argument(B2_ERRMSG("provided an invalid blake2b key"));
            }

            blake2b_param &param = mState.P[0];

            param.digest_length = BLAKE2B_BLOCKBYTES;
            param.key_length = static_cast<uint8_t>(key.size());
            param.fanout = 1;
            param.depth = 1;
            param.leaf_length = 0;
            param.node_offset = 0;
            param.xof_length = static_cast<uint32_t>(digestSize);
            param.node_depth = 0;
            param.inner_length = 0;
            fill_blob(blob{ param.reserved });
            fill_blob(blob{ param.salt });
            personalisation.copy_to(blob{ param.personal });

            safe_call(blake2b_init_param, B2_ERRMSG("blake2(x)b_init_param with personal failed"),
                mState.S, &param);

            init_key(key);

            return *this;
        }

        using blake2_base::update;
        blake2xb & update(blob_view data)
        {
            ctx_safe_call(blake2xb_update, B2_ERRMSG("blake2xb_update failed"),
                data.data(), data.size());
            return *this;
        }

        void final(blob digest)
        {
            ctx_safe_call(blake2xb_final, B2_ERRMSG("blake2xb_final failed"),
                digest.data(), digest.size());
        }

    private:
        blake2xb_state mState;
    };


    constexpr std::array<std::byte, blake2b::personal_bytes> vefs_blake2b_personalization
        = utils::make_byte_array<
            0x76, 0x65, 0x66, 0x73, 0xa4, 0xa1, 0x5f, 0x44,
            0xac, 0x08, 0x45, 0x31, 0x8d, 0x08, 0xd1, 0x33
        >();
    constexpr blob_view vefs_blake2b_personalization_view = blob_view{ vefs_blake2b_personalization };
}

#undef B2_ERRMSG
