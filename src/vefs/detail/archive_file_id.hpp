#pragma once

#include <cstddef>
#include <span>

#include <fmt/format.h>

#include <vefs/disappointment.hpp>
#include <vefs/hash/hash_algorithm.hpp>
#include <vefs/hash/spooky_v2.hpp>
#include <vefs/platform/sysrandom.hpp>
#include <vefs/span.hpp>
#include <vefs/utils/uuid.hpp>

namespace vefs::detail
{

struct file_id
{
    static const file_id archive_index;
    static const file_id free_block_index;

    constexpr file_id() noexcept = default;
    explicit constexpr file_id(uuid rawId) noexcept;
    explicit file_id(ro_blob<16> rawData) noexcept;

    auto as_uuid() const noexcept -> uuid;

    static auto generate() noexcept -> result<file_id>;

    inline friend auto operator==(file_id const &lhs,
                                  file_id const &rhs) noexcept -> bool
            = default;

private:
    uuid mId;
};

inline const file_id file_id::archive_index{
        uuid{{0xba, 0x22, 0xb0, 0x33, 0x4b, 0xa8, 0x4e, 0x5b, 0x83, 0x0c, 0xbf,
              0x48, 0x94, 0xaf, 0x53, 0xf8}}};
inline const file_id file_id::free_block_index{
        uuid{{0x33, 0x38, 0xbe, 0x54, 0x6b, 0x02, 0x49, 0x24, 0x9f, 0xcc, 0x56,
              0x3d, 0x7e, 0xe6, 0x81, 0xe6}}};

constexpr file_id::file_id(uuid rawId) noexcept
    : mId(rawId)
{
}
inline file_id::file_id(ro_blob<16> rawData) noexcept
    : mId(std::span<std::uint8_t, rawData.extent>(
            reinterpret_cast<std::uint8_t *>(
                    const_cast<std::byte *>(rawData.data())),
            rawData.extent))
{
}

inline auto file_id::as_uuid() const noexcept -> uuid
{
    return mId;
}

inline auto file_id::generate() noexcept -> result<file_id>
{
    std::uint8_t bytes[16] = {};
    VEFS_TRY(random_bytes(as_writable_bytes(std::span(bytes))));

    // variant must be 10xxxxxx
    bytes[8] &= 0b0011'1111;
    bytes[8] |= 0b1000'0000;

    // version must be 0100xxxx
    bytes[6] &= 0b0000'1111;
    bytes[6] |= 0b0100'0000;

    return file_id{uuid{bytes}};
}

} // namespace vefs::detail

template <>
struct std::hash<vefs::detail::file_id>
    : vefs::std_hash_for<vefs::spooky_v2_hash, vefs::detail::file_id>
{
};

template <>
struct fmt::formatter<vefs::detail::file_id>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext &ctx)
    {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(vefs::detail::file_id const &fid, FormatContext &ctx)
    {
        using char_type = typename FormatContext::char_type;

        auto out = ctx.out();
        auto const id = fid.as_uuid();
        auto const bytes = id.as_bytes();
        auto const data = reinterpret_cast<std::uint8_t const *>(bytes.data());

        for (std::size_t i = 0, limit = bytes.size(); i < limit; ++i)
        {
            using namespace vefs::detail;
            *out++ = guid_encoding_lut<char_type>[(data[i] >> 4) & 0x0f];
            *out++ = guid_encoding_lut<char_type>[data[i] & 0x0f];
            if (i == 3 || i == 5 || i == 7 || i == 9)
            {
                *out++ = guid_encoding_lut<char_type>[0x10];
            }
        }
        return out;
    }
};

namespace vefs::ed
{

enum class archive_file_id_tag
{
};
using archive_file_id
        = error_detail<archive_file_id_tag, vefs::detail::file_id>;

} // namespace vefs::ed
