#pragma once

#include <concepts>
#include <iterator>

#include <vefs/cache/cache_page.hpp>

namespace vefs::detail
{

// clang-format off
template <typename T>
concept eviction_policy
        = std::same_as<
                typename T::page_state,
                cache_page_state<typename T::key_type>
            >
        && std::forward_iterator<typename T::replacement_iterator>
        && std::same_as<
                typename std::iterator_traits<typename T::replacement_iterator>
                        ::value_type,
                typename T::page_state
            >
        &&  requires(T &&policy,
                     typename T::key_type const &key,
                     typename T::index_type idx,
                     typename T::index_type &indexOut,
                     typename T::page_state::state_type &generationOut,
                     typename T::replacement_iterator rit)
{
    typename T::replacement_iterator;

    { policy.begin() }
        -> std::same_as<typename T::replacement_iterator>;
    { policy.end() }
        -> std::same_as<typename T::replacement_iterator>;

    policy.num_managed();

    policy.insert(key, idx);
    { policy.on_access(key, idx) }
        -> std::same_as<bool>;

    { policy.try_evict(std::move(rit), indexOut, generationOut) }
        -> std::same_as<cache_replacement_result>;
    { policy.on_purge(key, idx) }
        -> std::same_as<bool>;
};
// clang-format on

} // namespace vefs::detail
