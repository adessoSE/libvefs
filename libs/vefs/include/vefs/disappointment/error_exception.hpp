#pragma once

#include <new>
#include <string>
#include <stdexcept>

#include <vefs/disappointment/error.hpp>

namespace vefs
{
    class error_exception final
        : public std::exception
    {
    public:
        error_exception() = delete;
        explicit error_exception(error err) noexcept;

        const char * what() const noexcept override;

    private:
        mutable error mErr;
        mutable std::string mErrDesc;
    };

    inline error_exception::error_exception(error err) noexcept
        : mErr{ std::move(err) }
        , mErrDesc{}
    {
    }
}
