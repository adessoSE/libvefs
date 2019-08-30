#pragma once

#include <cstddef>

#include <fmt/format.h>

#include <vefs/blob.hpp>
#include <vefs/exceptions.hpp>
#include <vefs/utils/uuid.hpp>
#include <vefs/utils/hash/default_weak.hpp>

namespace vefs::detail
{
    struct file_id
    {
        static const file_id archive_index;
        static const file_id free_block_index;

        file_id() noexcept = default;
        explicit file_id(utils::uuid raw_id) noexcept
            : mId(raw_id)
        {
        }
        explicit file_id(ro_blob<16> raw_data)
        {
            copy(raw_data, as_writable_bytes(span(mId.data)));
        }

        auto & as_uuid() const noexcept
        {
            return mId;
        }

        explicit operator bool() const noexcept
        {
            return !mId.is_nil();
        }

    private:
        utils::uuid mId;
    };

    inline bool operator==(const file_id &lhs, const file_id &rhs)
    {
        return lhs.as_uuid() == rhs.as_uuid();
    }
    inline bool operator!=(const file_id &lhs, const file_id &rhs)
    {
        return !(lhs == rhs);
    }

    template <typename Impl, typename H>
    inline void compute_hash(const file_id &obj, H &h, utils::hash::algorithm_tag<Impl>)
    {
        utils::compute_hash(obj.as_uuid(), h, utils::hash::algorithm_tag<Impl>{});
    }
    template <typename Impl>
    inline void compute_hash(const file_id &obj, Impl &state)
    {
        utils::compute_hash(obj.as_uuid(), state);
    }
}

namespace std
{
    template <>
    struct hash<vefs::detail::file_id>
        : vefs::utils::hash::default_weak_std<vefs::detail::file_id>
    {
    };
}

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
            decltype(auto) id = fid.as_uuid();
            auto out = ctx.out();
            for (std::size_t i = 0; i < id.static_size(); ++i)
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
}

namespace vefs::ed
{
    enum class archive_file_id_tag {};
    using archive_file_id = error_detail<archive_file_id_tag, vefs::detail::file_id>;
}
