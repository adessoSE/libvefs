#pragma once

#include <cstddef>

#include <span>

#include <dplx/dp/concepts.hpp>
#include <dplx/dp/encoder/api.hpp>
#include <dplx/dp/encoder/core.hpp>
#include <dplx/dp/fwd.hpp>
#include <dplx/dp/item_parser.hpp>

namespace dplx::dp
{

template <output_stream Stream>
class basic_encoder<vefs::uuid, Stream>
{
    using emit = item_emitter<Stream>;

public:
    using value_type = vefs::uuid;

    inline auto operator()(Stream &outStream, value_type value) const
            -> result<void>
    {
        return encode(outStream, value.as_bytes());
    }
};

template <input_stream Stream>
class basic_decoder<vefs::uuid, Stream>
{
    using parse = item_parser<Stream>;

public:
    using value_type = vefs::uuid;

    inline auto operator()(Stream &inStream, value_type &value) const
            -> result<void>
    {
        std::array<std::uint8_t, 16> data{};
        DPLX_TRY(auto const size,
                 parse::binary(inStream, data, parse_mode::canonical));

        if (size != data.size())
        {
            return errc::tuple_size_mismatch;
        }
        value = data;
        return oc::success();
    }
};

constexpr auto tag_invoke(encoded_size_of_fn, vefs::uuid const &) noexcept
        -> unsigned
{
    // return dp::additional_information_size(vefs::utils::uuid::static_size())
    //     + vefs::utils::uuid::static_size();
    return 17U;
}

} // namespace dplx::dp
