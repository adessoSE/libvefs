#pragma once

#include <vefs/utils/binary_codec.hpp>

#include "root_sector_info.hpp"
#include "sector_device.hpp"

namespace vefs::detail
{
    class reference_sector_layout
    {
    public:
        static constexpr std::size_t serialized_reference_size = 32;
        static constexpr std::size_t references_per_sector =
            sector_device::sector_payload_size / serialized_reference_size;

        explicit reference_sector_layout(rw_blob<sector_device::sector_payload_size> data) noexcept
            : mCodec(data)
        {
        }

        inline auto read(int which) const noexcept -> sector_reference
        {
            const auto baseOffset = static_cast<std::size_t>(which) * serialized_reference_size;

            sector_reference deserialized;
            deserialized.sector = mCodec.read<sector_id>(baseOffset);
            copy(mCodec.as_bytes().subspan(baseOffset + 16, 16), span(deserialized.mac));

            return deserialized;
        }

        inline void write(int which, sector_reference reference) noexcept
        {
            const auto baseOffset = static_cast<std::size_t>(which) * serialized_reference_size;

            mCodec.write(reference.sector, baseOffset);
            copy(span(reference.mac), mCodec.as_writeable_bytes().subspan(baseOffset + 16, 16));
        }

    private:
        utils::binary_codec<sector_device::sector_payload_size> mCodec;
    };
} // namespace vefs::detail
