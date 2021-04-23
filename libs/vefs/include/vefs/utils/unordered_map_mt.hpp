#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>

#include <libcuckoo/cuckoohash_map.hh>
#include <vefs/utils/hash/default_weak.hpp>

namespace vefs::utils
{
template <typename Key,
          typename T,
          typename Hash = hash::default_weak_std<Key>,
          typename KeyEqual = std::equal_to<>,
          typename Allocator = std::allocator<std::pair<const Key, T>>,
          std::size_t SLOT_PER_BUCKET = libcuckoo::DEFAULT_SLOT_PER_BUCKET>
using unordered_map_mt = libcuckoo::
        cuckoohash_map<Key, T, Hash, KeyEqual, Allocator, SLOT_PER_BUCKET>;

template <typename T,
          typename Allocator = std::allocator<std::pair<const std::string, T>>,
          std::size_t SLOT_PER_BUCKET = libcuckoo::DEFAULT_SLOT_PER_BUCKET>
using unordered_string_map_mt = libcuckoo::cuckoohash_map<
        std::string,
        T,
        utils::hash::default_weak_std<std::string_view>,
        std::equal_to<>,
        Allocator,
        SLOT_PER_BUCKET>;
} // namespace vefs::utils
