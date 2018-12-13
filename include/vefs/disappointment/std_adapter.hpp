#pragma once

#include <system_error>

#include <vefs/disappointment/fwd.hpp>

namespace vefs::adl::disappointment
{
    auto make_error(std::error_code ec, adl::disappointment::type) noexcept
        -> error;

    auto make_error(std::errc ec, adl::disappointment::type) noexcept
        -> error;
}
