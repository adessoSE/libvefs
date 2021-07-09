#pragma once

#include <memory>

namespace vefs
{

class vfilesystem;
class vfile;
using vfile_handle = std::shared_ptr<vfile>;

class archive_handle;

} // namespace vefs

namespace vefs::detail
{

class thread_pool;

class archive_sector_allocator;

class sector_device;

class pooled_work_tracker;

} // namespace vefs::detail

namespace vefs::crypto
{

class crypto_provider;

// mirroring <vefs/crypto/provider.hpp>
auto boringssl_aes_256_gcm_crypto_provider() -> crypto_provider *;

} // namespace vefs::crypto
