#include <vefs/platform/secure_memzero.hpp>

#include <openssl/mem.h>

namespace vefs::utils
{
void secure_memzero(rw_dynblob data)
{
    if (!data.empty())
    {
        OPENSSL_cleanse(data.data(), data.size());
    }
}
} // namespace vefs::utils
