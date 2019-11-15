#pragma once

#include <cstddef>

#include <fmt/format.h>

#include <vefs/span.hpp>
#include <vefs/utils/hash/default_weak.hpp>
#include <vefs/utils/uuid.hpp>

namespace vefs::detail
{
    struct file_id
    {
        static const file_id archive_index;
        static const file_id free_block_index;

        file_id() noexcept = default;
        explicit file_id(utils::uuid rawId) noexcept;
        explicit file_id(ro_blob<16> rawData) noexcept;

        auto as_uuid() const noexcept -> utils::uuid;

        inline friend auto operator==(const file_id &lhs, const file_id &rhs) noexcept -> bool
        {
            return lhs.mId == rhs.mId;
        }

    private:
        utils::uuid mId;
    };

    inline const file_id file_id::archive_index{utils::uuid{0xba, 0x22, 0xb0, 0x33, 0x4b, 0xa8,
                                                            0x4e, 0x5b, 0x83, 0x0c, 0xbf, 0x48,
                                                            0x94, 0xaf, 0x53, 0xf8}};
    inline const file_id file_id::free_block_index{utils::uuid{0x33, 0x38, 0xbe, 0x54, 0x6b, 0x02,
                                                               0x49, 0x24, 0x9f, 0xcc, 0x56, 0x3d,
                                                               0x7e, 0xe6, 0x81, 0xe6}};

    inline file_id::file_id(utils::uuid rawId) noexcept
        : mId(rawId)
    {
    }
    inline file_id::file_id(ro_blob<16> rawData) noexcept
    {
        copy(rawData, as_writable_bytes(span(mId.data)));
    }

    inline auto file_id::as_uuid() const noexcept -> utils::uuid
    {
        return mId;
    }

    inline auto operator!=(const file_id &lhs, const file_id &rhs) noexcept -> bool
    {
        return !(lhs == rhs);
    }

    template <typename Impl, typename H>
    inline void compute_hash(const file_id &obj, H &h, utils::hash::algorithm_tag<Impl>) noexcept
    {
        utils::compute_hash(obj.as_uuid(), h, utils::hash::algorithm_tag<Impl>{});
    }
    template <typename Impl>
    inline void compute_hash(const file_id &obj, Impl &state) noexcept
    {
        utils::compute_hash(obj.as_uuid(), state);
    }
} // namespace vefs::detail

namespace std
{
    template <>
    struct hash<vefs::detail::file_id> : vefs::utils::hash::default_weak_std<vefs::detail::file_id>
    {
    };
} // namespace std

namespace fmt
{
    template <>
    struct formatter<vefs::detail::file_id>
    {
        template <typename ParseContext>
        constexpr auto parse(ParseContext &ctx)
        {
            return ctx.begin();
        }

        template <typename FormatContext>
        auto format(const vefs::detail::file_id &fid, FormatContext &ctx)
        {
            auto &&id = fid.as_uuid();
            auto out = ctx.out();
            constexpr std::size_t limit = id.static_size();
            for (std::size_t i = 0; i < limit; ++i)
            {
                out = format_to(out, "{:02X}", id.data[i]);
                if (i == 3 || i == 5 || i == 7 || i == 9)
                {
                    *out++ = '-';
                }
            }
            return out;
        }
    };
} // namespace fmt

namespace vefs::ed
{
    enum class archive_file_id_tag
    {
    };
    using archive_file_id = error_detail<archive_file_id_tag, vefs::detail::file_id>;
} // namespace vefs::ed
