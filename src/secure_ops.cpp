#include "precompiled.hpp"
#include <vefs/utils/secure_ops.hpp>
#include <vefs/utils/secure_array.hpp>

#include <openssl/mem.h>


namespace vefs::utils
{
    void secure_memzero(blob data)
    {
        if (data)
        {
            OPENSSL_cleanse(data.data(), data.size());
        }
    }
}
