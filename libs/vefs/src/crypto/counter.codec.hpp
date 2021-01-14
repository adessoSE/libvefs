#pragma once

#include <dplx/dp/decoder/api.hpp>
#include <dplx/dp/decoder/core.hpp>
#include <dplx/dp/encoder/api.hpp>
#include <dplx/dp/encoder/core.hpp>

#include "../detail/cbor_utils.hpp"
#include "../detail/secure_array.codec.hpp"
#include "counter.hpp"

namespace dplx::dp
{
    template <input_stream Stream>
    class basic_decoder<vefs::crypto::counter, Stream>
    {
    public:
        using value_type = vefs::crypto::counter;

        inline auto operator()(Stream &inStream, value_type &value) const
            -> result<void>
        {
            DPLX_TRY(auto &&headInfo, detail::parse_item_info(inStream));

            if (std::byte{headInfo.type} != type_code::binary)
            {
                return errc::item_type_mismatch;
            }
            if (headInfo.value != 16)
            {
                return errc::invalid_additional_information;
            }

            DPLX_TRY(auto readProxy, read(inStream, 16));
            value = value_type(vefs::ro_blob<16>(readProxy));

            if constexpr (lazy_input_stream<Stream>)
            {
                DPLX_TRY(consume(inStream, readProxy));
            }

            return success();
        }
    };

    template <input_stream Stream>
    class basic_decoder<std::atomic<vefs::crypto::counter>, Stream>
    {
    public:
        using value_type = std::atomic<vefs::crypto::counter>;

        inline auto operator()(Stream &inStream, value_type &value) const
            -> result<void>
        {
            using counter_type = typename value_type::value_type;
            DPLX_TRY(auto nested, decode(as_value<counter_type>, inStream));

            value.store(nested);
            return success();
        }
    };

    template <output_stream Stream>
    class basic_encoder<vefs::crypto::counter, Stream>
    {
    public:
        using value_type = vefs::crypto::counter;

        inline auto operator()(Stream &outStream, value_type const &value) const
            -> result<void>
        {
            return encode(outStream,
                          std::span<std::byte const, 16>(value.view()));
        }
    };

    template <output_stream Stream>
    class basic_encoder<std::atomic<vefs::crypto::counter>, Stream>
    {
    public:
        using value_type = std::atomic<vefs::crypto::counter>;

        inline auto operator()(Stream &outStream, value_type const &value) const
            -> result<void>
        {
            return encode(outStream, value.load());
        }
    };
} // namespace dplx::dp
