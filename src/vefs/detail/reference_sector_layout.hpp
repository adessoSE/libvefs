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
    static constexpr std::size_t references_per_sector
            = sector_device::sector_payload_size / serialized_reference_size;

    explicit reference_sector_layout(
            rw_blob<sector_device::sector_payload_size> data) noexcept
        : mCodec(data)
    {
    }

    [[nodiscard]] inline auto read(int which) const noexcept -> sector_reference
    {
        auto const baseOffset
                = static_cast<std::size_t>(which) * serialized_reference_size;

        sector_reference deserialized;
        deserialized.sector = mCodec.read<sector_id>(baseOffset);
        copy(mCodec.as_bytes().subspan(baseOffset + 16, 16),
             std::span(deserialized.mac));

        return deserialized;
    }
    static auto read(ro_blob<sector_device::sector_payload_size> sectorContent,
                     int which) noexcept -> sector_reference
    {
        auto const baseOffset
                = static_cast<std::size_t>(which) * serialized_reference_size;

        sector_reference deserialized;
        deserialized.sector
                = ::vefs::load_primitive<sector_id>(sectorContent, baseOffset);
        ::vefs::copy(sectorContent.subspan(baseOffset + 16U, 16U),
                     std::span(deserialized.mac));

        return deserialized;
    }

    inline void write(int which, sector_reference reference) noexcept
    {
        auto const baseOffset
                = static_cast<std::size_t>(which) * serialized_reference_size;

        mCodec.write(reference.sector, baseOffset);
        mCodec.write<std::uint64_t>(0, baseOffset + 8);
        copy(std::span(reference.mac),
             mCodec.as_writeable_bytes().subspan(baseOffset + 16, 16));
    }
    static void write(rw_blob<sector_device::sector_payload_size> sectorContent,
                      int which,
                      sector_reference reference) noexcept
    {
        auto const baseOffset
                = static_cast<std::size_t>(which) * serialized_reference_size;

        ::vefs::store_primitive(sectorContent, reference.sector, baseOffset);
        ::vefs::store_primitive<std::uint64_t>(sectorContent, 0U,
                                               baseOffset + 8U);
        ::vefs::copy(std::span(reference.mac),
                     sectorContent.subspan(baseOffset + 16U, 16U));
    }

private:
    utils::binary_codec<sector_device::sector_payload_size> mCodec;
};
} // namespace vefs::detail
