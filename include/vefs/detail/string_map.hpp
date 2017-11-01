#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <vefs/ext/libcuckoo/cuckoohash_map.hh>

namespace vefs::detail
{
    using shared_string = std::shared_ptr<std::string>;
    struct shared_string_equality
    {
        bool operator()(const shared_string &lhs, const shared_string &rhs)
        {
            return *lhs == *rhs;
        }

        bool operator()(std::string_view lhs, const shared_string &rhs)
        {
            return lhs == *rhs;
        }

        bool operator()(const shared_string &lhs, std::string_view rhs)
        {
            return *lhs == rhs;
        }
    };
    struct shared_string_hash
    {
        std::size_t operator()(const shared_string &value)
        {
            return std::hash<std::string>()(*value);
        }

        std::size_t operator()(std::string_view value)
        {
            return std::hash<std::string_view>()(value);
        }
    };

    template <typename T,
        typename Allocator = std::allocator<std::pair<const shared_string, T>>,
        std::size_t SLOT_PER_BUCKET = LIBCUCKOO_DEFAULT_SLOT_PER_BUCKET>
    using string_map = cuckoohash_map< shared_string, T,
        shared_string_hash, shared_string_equality, Allocator, SLOT_PER_BUCKET>;
}
