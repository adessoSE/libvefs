#pragma once

#include <vefs/utils/hash/algorithm_tag.hpp>
#include <vefs/utils/hash/detail/spooky.hpp>
#include <vefs/utils/hash/detail/std_adaptor.hpp>

namespace vefs::utils::hash
{
using default_weak = detail::spooky;
using default_weak_tag = algorithm_tag<default_weak>;

template <typename T>
using default_weak_std = detail::std_adaptor<T, default_weak>;
} // namespace vefs::utils::hash
