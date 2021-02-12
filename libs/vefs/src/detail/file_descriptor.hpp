#pragma once

#include <memory>
#include <string>

#include <vefs/platform/secure_memzero.hpp>
#include <vefs/utils/secure_array.hpp>
#include <vefs/utils/uuid.hpp>

#include "file_crypto_ctx.hpp"
#include "root_sector_info.hpp"
#include "sector_id.hpp"

namespace vefs::detail
{
struct file_descriptor
{
    utils::uuid fileId;

    std::array<std::byte, 32> secret;
    crypto::counter secretCounter;
    root_sector_info data;

    std::u8string filePath;

    // ISO 8601 encoded date time string
    std::u8string modificationTime;

    ~file_descriptor() noexcept
    {
        utils::secure_data_erase(secret);
        utils::secure_data_erase(fileId);
        utils::secure_data_erase(data);
    }

    file_descriptor(file_descriptor &&) noexcept = default;
    auto operator=(file_descriptor &&) noexcept -> file_descriptor & = default;

    file_descriptor() noexcept = default;
    file_descriptor(utils::uuid id,
                    file_crypto_ctx const &ctx,
                    root_sector_info root) noexcept
        : fileId(id)
        , data(root)
        , filePath()
        , modificationTime()
    {
        auto ctxState = ctx.state();
        vefs::copy(as_span(ctxState.secret), span(secret));
        secretCounter = ctxState.counter;
    }
};

} // namespace vefs::detail
