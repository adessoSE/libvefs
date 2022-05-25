#pragma once

#include <system_error>

#include <vefs/disappointment/fwd.hpp>

namespace vefs::adl::disappointment
{
/*
converts std::error_code to error
*/
auto make_error(std::error_code ec, adl::disappointment::type) noexcept
        -> error;

/*
converts std::errc to error
*/
auto make_error(std::errc ec, adl::disappointment::type) noexcept -> error;
} // namespace vefs::adl::disappointment
