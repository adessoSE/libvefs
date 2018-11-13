#pragma once

#include <new>
#include <string>
#include <stdexcept>

#include <vefs/disappointment/error_info.hpp>

namespace vefs
{
    class error_exception final
        : public std::exception
    {
    public:
        error_exception() = delete;
        error_exception(error_info err) noexcept;

        const char * what() const noexcept override;

    private:
        error_info mErr;
        mutable std::string mErrDesc;
    };

    inline error_exception::error_exception(error_info err) noexcept
        : mErr{ std::move(err) }
        , mErrDesc{}
    {
    }
}
