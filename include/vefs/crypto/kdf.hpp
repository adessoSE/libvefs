#pragma once

#include <cstddef>

#include <vector>
#include <type_traits>
#include <initializer_list>

#include <vefs/blob.hpp>
#include <vefs/utils/secure_array.hpp>

namespace vefs::crypto
{
    void kdf(blob_view key, const std::vector<blob_view> &domain, blob outputPRK);
    void kdf(blob_view key, std::initializer_list<blob_view> domain, blob outputPRK);
    void kdf(blob_view key, blob_view domain, blob outputPRK);

    template <typename... Args>
    std::vector<std::byte> kdf(Args... args, std::size_t outputPRKSize)
    {
        std::vector<std::byte> prk(outputPRKSize);
        kdf(std::forward<Args>(args)..., blob{ prk });
        return prk;
    }

    template <std::size_t PrkSize,  typename... Args>
    utils::secure_byte_array<PrkSize> kdf(Args... args)
    {
        utils::secure_byte_array<PrkSize> prk;
        kdf(std::forward<Args>(args)..., blob{ prk });
        return prk;
    }

    template <std::size_t PrkSize>
    utils::secure_byte_array<PrkSize> kdf(blob_view key, std::initializer_list<blob_view> domain)
    {
        utils::secure_byte_array<PrkSize> prk;
        kdf(key, domain, blob{ prk });
        return prk;
    }
}

