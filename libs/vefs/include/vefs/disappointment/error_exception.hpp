#pragma once

#include <new>
#include <stdexcept>
#include <string>

#include <vefs/disappointment/error.hpp>

namespace vefs
{
    class error_exception final : public std::exception
    {
    public:
        error_exception() = delete;
        explicit error_exception(vefs::error err) noexcept;

        const char *what() const noexcept override;

        auto error() noexcept -> vefs::error;

    private:
        mutable vefs::error mErr;
        mutable std::string mErrDesc;
    };

    inline error_exception::error_exception(vefs::error err) noexcept
        : mErr{std::move(err)}
        , mErrDesc{}
    {
    }

    inline auto error_exception::error() noexcept -> vefs::error
    {
        return mErr;
    }
} // namespace vefs
