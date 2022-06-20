#include "vefs/hash/spooky_v2.hpp"

#include <vefs/platform/sysrandom.hpp>

namespace vefs
{

auto spooky_v2_hash::generate_key() noexcept -> key_type
{
    key_type key{};
    (void)detail::random_bytes(rw_blob_cast(key));
    return key;
}
void spooky_v2_hash::generate_keys(std::span<key_type> const keys) noexcept
{
    (void)detail::random_bytes(as_writable_bytes(keys));
}
// auto spooky_v2_hash::geK

} // namespace vefs
