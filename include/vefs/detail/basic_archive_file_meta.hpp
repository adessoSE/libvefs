#pragma once

#include <cstdint>

#include <memory>
#include <string>
#include <shared_mutex>

#include <vefs/blob.hpp>
#include <vefs/utils/uuid.hpp>
#include <vefs/utils/secure_array.hpp>
#include <vefs/utils/hash/default_weak.hpp>
#include <vefs/crypto/counter.hpp>
#include <vefs/detail/archive_file_id.hpp>
#include <vefs/detail/sector_id.hpp>

namespace vefs::detail
{
    struct basic_archive_file_meta
    {
        basic_archive_file_meta() = default;
        inline basic_archive_file_meta(basic_archive_file_meta &&other);

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

        utils::secure_byte_array<32> secret;
        std::atomic<crypto::counter> write_counter;
        std::array<std::byte, 16> start_block_mac;

        file_id id;

        sector_id start_block_idx;
        std::uint64_t size;
        int tree_depth;
    };

    inline basic_archive_file_meta::basic_archive_file_meta(basic_archive_file_meta &&other)
        : secret{ other.secret }
        , write_counter{ other.write_counter.load().value() }
        , start_block_mac{ other.start_block_mac }
        , id{ other.id }
        , start_block_idx{ other.start_block_idx }
        , size{ other.size }
        , tree_depth{ other.tree_depth }
    {
        other.secret = {};
        other.write_counter.store(crypto::counter{});
        other.start_block_idx = sector_id::master;
        other.start_block_mac = {};
        other.size = 0;
        other.tree_depth = -1;
    }
}
