#pragma once

#include <dplx/dp/decoder/api.hpp>
#include <dplx/dp/decoder/core.hpp>
#include <dplx/dp/encoder/api.hpp>
#include <dplx/dp/encoder/core.hpp>

#include "../detail/secure_array.codec.hpp"
#include "counter.hpp"

namespace dplx::dp
{
template <input_stream Stream>
class basic_decoder<vefs::crypto::counter, Stream>
{
    using parse = dp::item_parser<Stream>;

public:
    using value_type = vefs::crypto::counter;

    inline auto operator()(Stream &inStream, value_type &value) const
            -> result<void>
    {
        vefs::utils::secure_byte_array<value_type::state_size> state{};
        DPLX_TRY(auto xsize,
                 parse::binary_finite(inStream, state, value_type::state_size,
                                      parse_mode::canonical));

        if (xsize != value_type::state_size)
        {
            return errc::item_value_out_of_range;
        }
        value = value_type(vefs::ro_blob<value_type::state_size>(state));
        return oc::success();
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
        return encode(outStream, std::span<std::byte const, 16>(value.view()));
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
