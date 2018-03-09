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
#include <vefs/exceptions.hpp>
#include <vefs/utils/secure_array.hpp>
#include <vefs/utils/misc.hpp>


namespace vefs::crypto::detail
{
    class blake2_api_error
        : public virtual vefs::crypto_failure
    {
    };

    // #TODO replace blake2_invalid_argument with invalid_argument
    class blake2_invalid_argument
        : public std::invalid_argument
    {
        using std::invalid_argument::invalid_argument;
    };

    template< typename D >
    class blake2_base
    {
        D * self()
        {
            return static_cast<D *>(this);
        }

    protected:
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
                BOOST_THROW_EXCEPTION(blake2_invalid_argument{ "requested an invalid digest size" });
            }

            if (blake2b_init(&mState, digestSize))
            {
                BOOST_THROW_EXCEPTION(blake2_api_error{}
                    << errinfo_api_function{ "blake2b_init" });
            }
            return *this;
        }
        blake2b& init(std::size_t digestSize, blob_view key)
        {
            if (!digestSize || digestSize < 16 || digestSize > block_bytes)
            {
                BOOST_THROW_EXCEPTION(blake2_invalid_argument{ "requested an invalid digest size" });
            }
            if (!key || key.size() > max_key_bytes)
            {
                BOOST_THROW_EXCEPTION(blake2_invalid_argument{ "provided an invalid blake2b key" });
            }

            if (blake2b_init_key(&mState, digestSize, key.data(), key.size()))
            {
                BOOST_THROW_EXCEPTION(blake2_api_error{}
                    << errinfo_api_function{ "blake2b_init_key" });
            }
            return *this;
        }

        blake2b& init(std::size_t digestSize, blob_view key, blob_view personalisation)
        {
            if (!digestSize || digestSize < 16 || digestSize > block_bytes)
            {
                BOOST_THROW_EXCEPTION(blake2_invalid_argument{ "requested an invalid digest size" });
            }
            if (!personalisation || personalisation.size() != sizeof(std::declval<blake2b_param>().personal))
            {
                BOOST_THROW_EXCEPTION(blake2_invalid_argument{ "provided an invalid personalisation blob" });
            }
            if (key.size() > max_key_bytes)
            {
                BOOST_THROW_EXCEPTION(blake2_invalid_argument{ "provided an invalid blake2b key" });
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

            if (blake2b_init_param(&mState, &param))
            {
                BOOST_THROW_EXCEPTION(blake2_api_error{}
                    << errinfo_api_function{ "blake2b_init_param" });
            }

            if (key)
            {
                init_key(key);
            }

            return *this;
        }

        using blake2_base::update;
        blake2b& update(blob_view data)
        {
            if (blake2b_update(&mState, data.data(), data.size()))
            {
                BOOST_THROW_EXCEPTION(blake2_api_error{}
                    << boost::errinfo_api_function{ "blake2b_update" });
            }
            return *this;
        }

        void final(blob digest)
        {
            if (blake2b_final(&mState, digest.data(), digest.size()))
            {
                BOOST_THROW_EXCEPTION(blake2_api_error{}
                    << boost::errinfo_api_function{ "blake2b_final" });
            }
        }

    private:
        blake2b_state mState;
    };

    class blake2xb
        : public blake2_base<blake2xb>
    {
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
                BOOST_THROW_EXCEPTION(blake2_invalid_argument{ "requested an invalid digest size" });
            }

            if (blake2xb_init(&mState, digestSize))
            {
                BOOST_THROW_EXCEPTION(blake2_api_error{}
                    << boost::errinfo_api_function{ "blake2xb_init" });
            }
            return *this;
        }
        blake2xb & init(std::size_t digestSize, blob_view key)
        {
            if (!digestSize || digestSize > variable_digest_length)
            {
                BOOST_THROW_EXCEPTION(blake2_invalid_argument{ "requested an invalid digest size" });
            }
            if (!key || key.size() > max_key_bytes)
            {
                BOOST_THROW_EXCEPTION(blake2_invalid_argument{ "provided an invalid blake2b key" });
            }

            if (blake2xb_init_key(&mState, digestSize, key.data(), key.size()))
            {
                BOOST_THROW_EXCEPTION(blake2_api_error{}
                    << errinfo_api_function{ "blake2xb_init_key" });
            }
            return *this;
        }
        blake2xb & init(std::size_t digestSize, blob_view key, blob_view personalisation)
        {
            if (!digestSize || digestSize > variable_digest_length)
            {
                BOOST_THROW_EXCEPTION(blake2_invalid_argument{ "requested an invalid digest size" });
            }
            if (!personalisation || personalisation.size() != sizeof(std::declval<blake2b_param>().personal))
            {
                BOOST_THROW_EXCEPTION(blake2_invalid_argument{ "provided an invalid personalisation blob" });
            }
            if (key.size() > max_key_bytes)
            {
                BOOST_THROW_EXCEPTION(blake2_invalid_argument{ "provided an invalid blake2b key" });
            }

            blake2b_param &param = mState.P[0];

            param.digest_length = BLAKE2B_OUTBYTES;
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

            if (blake2b_init_param(mState.S, &param))
            {
                BOOST_THROW_EXCEPTION(blake2_api_error{}
                    << errinfo_api_function{ "blake2b_init_param" });
            }

            if (key)
            {
                init_key(key);
            }

            return *this;
        }

        using blake2_base::update;
        blake2xb & update(blob_view data)
        {
            if (blake2xb_update(&mState, data.data(), data.size()))
            {
                BOOST_THROW_EXCEPTION(blake2_api_error{}
                << boost::errinfo_api_function{ "blake2xb_update" });
            }
            return *this;
        }

        void final(blob digest)
        {
            if (blake2xb_final(&mState, digest.data(), digest.size()))
            {
                BOOST_THROW_EXCEPTION(blake2_api_error{}
                << boost::errinfo_api_function{ "blake2xb_final" });
            }
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
