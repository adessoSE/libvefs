#pragma once

#include <type_traits>
#include <utility>

#include <boost/outcome/success_failure.hpp>

#include <vefs/disappointment/fwd.hpp>
#include <vefs/llfio.hpp>

namespace vefs::adl::disappointment
{
    /**
     * converts llfio::error_info to vefs::error
     */
    auto make_error(const llfio::error_info &info,
                    adl::disappointment::type) noexcept -> error;
} // namespace vefs::adl::disappointment

namespace boost::outcome_v2
{
    template <typename T>
    inline decltype(auto) try_operation_return_as(vefs::llfio::result<T> &&v)
    {
        return boost::outcome_v2::failure(std::move(v).assume_error());
    }

    template <typename T>
    inline decltype(auto)
    try_operation_return_as(vefs::llfio::io_handle::io_result<T> &&v)
    {
        return boost::outcome_v2::failure(std::move(v).assume_error());
    }
} // namespace boost::outcome_v2
