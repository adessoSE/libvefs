#pragma once

#include <cstddef>

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
        explicit file_id(blob_view raw_data)
        {
            if (raw_data.size() != mId.size())
            {
                BOOST_THROW_EXCEPTION(logic_error{}
                    << errinfo_param_name{ "raw_data" }
                    << errinfo_param_misuse_description{ "data size mismatch (!= 16b)" }
                );
            }
            raw_data.copy_to(blob{ reinterpret_cast<std::byte*>(mId.data), mId.static_size() });
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
