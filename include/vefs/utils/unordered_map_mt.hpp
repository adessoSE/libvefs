#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>

#include <boost/predef/compiler.h>

#if defined(BOOST_COMP_MSVC_AVAILABLE)
#pragma warning(push, 3)
#endif

#include <libcuckoo/cuckoohash_map.hh>

#if defined(BOOST_COMP_MSVC_AVAILABLE)
#pragma warning(pop)
#endif

#include <vefs/hash/hash-std.hpp>
#include <vefs/hash/hash_algorithm.hpp>
#include <vefs/hash/spooky_v2.hpp>

namespace vefs::utils
{
template <typename Key,
          typename T,
          typename Hash = std_hash_for<spooky_v2_hash, Key>,
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
        std_hash_for<spooky_v2_hash, std::string_view>,
        std::equal_to<>,
        Allocator,
        SLOT_PER_BUCKET>;
} // namespace vefs::utils
