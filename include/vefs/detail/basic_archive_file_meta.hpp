#pragma once

#include <cstdint>

#include <memory>
#include <string>
#include <shared_mutex>

#include <boost/functional/hash.hpp>

#include <vefs/blob.hpp>
#include <vefs/utils/uuid.hpp>
#include <vefs/utils/secure_array.hpp>
#include <vefs/utils/hash/default_weak.hpp>
#include <vefs/crypto/counter.hpp>
#include <vefs/detail/sector_id.hpp>

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
                BOOST_THROW_EXCEPTION(logic_error{});
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


    struct basic_archive_file_meta
    {
        basic_archive_file_meta()
            : integrity_mutex()
            , shrink_mutex()
        {
        }

        blob_view secret_view() const
        {
            return blob_view{ secret };
        }

        blob start_block_mac_blob()
        {
            return blob{ start_block_mac };
        }
        blob_view start_block_mac_blob() const
        {
            return blob_view{ start_block_mac };
        }

        // always lock the shrink_mutex first!
        mutable std::shared_mutex integrity_mutex;
        mutable std::shared_mutex shrink_mutex;

        utils::secure_byte_array<32> secret;
        crypto::atomic_counter write_counter;
        std::array<std::byte, 16> start_block_mac;

        file_id id;

        sector_id start_block_idx;
        std::uint64_t size;
        int tree_depth;
        bool valid = true;
    };
}

namespace std
{
    template <>
    struct hash<vefs::detail::file_id>
        : vefs::utils::hash::default_weak_std<vefs::detail::file_id>
    {
    };
}
