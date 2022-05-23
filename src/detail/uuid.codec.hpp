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
class basic_encoder<vefs::utils::uuid, Stream>
{
public:
    using value_type = vefs::utils::uuid;

    inline auto operator()(Stream &outStream, value_type value) const
            -> result<void>
    {
        return encode(outStream, as_bytes(std::span<std::uint8_t>(value.data)));
    }
};

template <input_stream Stream>
class basic_decoder<vefs::utils::uuid, Stream>
{
    using parse = item_parser<Stream>;

public:
    using value_type = vefs::utils::uuid;

    inline auto operator()(Stream &inStream, value_type &value) const
            -> result<void>
    {
        std::span<std::uint8_t> data(value.data);
        DPLX_TRY(auto size,
                 parse::binary(inStream, data, parse_mode::canonical));

        if (size != value.size())
        {
            return errc::tuple_size_mismatch;
        }
        return oc::success();
    }
};

constexpr auto tag_invoke(encoded_size_of_fn,
                          vefs::utils::uuid const &) noexcept -> unsigned
{
    // return dp::additional_information_size(vefs::utils::uuid::static_size())
    //     + vefs::utils::uuid::static_size();
    return 17U;
}

} // namespace dplx::dp
