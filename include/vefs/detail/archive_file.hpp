#pragma once

#include <cstdint>

#include <memory>
#include <string>
#include <shared_mutex>

#include <boost/functional/hash.hpp>

#include <vefs/blob.hpp>
#include <vefs/crypto/counter.hpp>
#include <vefs/utils/secure_array.hpp>
#include <vefs/utils/uuid.hpp>
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
        alignas(16) utils::uuid mId;
    };

    inline bool operator==(const file_id &lhs, const file_id &rhs)
    {
        return lhs.as_uuid() == rhs.as_uuid();
    }
    inline bool operator!=(const file_id &lhs, const file_id &rhs)
    {
        return !(lhs == rhs);
    }


    struct raw_archive_file
    {
        raw_archive_file() = default;

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

        mutable std::shared_mutex integrity_mutex;

        utils::secure_byte_array<32> secret;
        crypto::atomic_counter write_counter;
        std::array<std::byte, 16> start_block_mac;

        file_id id;

        sector_id start_block_idx;
        std::uint64_t size;
        int tree_depth;
    };
}

namespace std
{
    template <>
    struct hash<vefs::detail::file_id>
    {
        std::size_t operator()(const vefs::detail::file_id &id)
        {
            return boost::hash<boost::uuids::uuid>{}(id.as_uuid());
        }
    };
}
