#pragma once

#include <variant>
#include <exception>
#include <system_error>

#include <vefs/exceptions.hpp>

namespace vefs::utils
{
    class async_error_info
    {
        using store_t = std::variant<std::monostate, std::error_code, boost::exception_ptr>;

    public:
        enum class type
        {
            none,
            code,
            exception,
        };

        async_error_info() = default;
        inline async_error_info(std::error_code ec);
        inline async_error_info(boost::exception_ptr exc);

        inline async_error_info & operator=(std::error_code ec);
        inline async_error_info & operator=(boost::exception_ptr exc);

        inline explicit operator bool() const;

        inline type which() const;
        inline bool is_code() const;
        inline bool is_exception() const;

        inline const std::error_code & get_code() const;
        inline const boost::exception_ptr & get_exception() const;

    private:
        store_t mStore;
    };

    template <typename Fn, typename... Args>
    async_error_info async_error_context(Fn &&fn, Args&&... args)
    {
        try
        {
            fn(std::forward<Args>(args)...);
        }
        catch (const boost::exception &exc)
        {
            if (auto codeptr = boost::get_error_info<errinfo_code>(exc))
            {
                return { *codeptr };
            }
            else
            {
                try
                {
                    throw;
                }
                catch (...)
                {
                    return { boost::current_exception() };
                }
            }
        }
        catch (...)
        {
            return { boost::current_exception() };
        }
        return {};
    }

    inline async_error_info::async_error_info(std::error_code ec)
        : mStore{ ec }
    {
    }

    inline async_error_info::async_error_info(boost::exception_ptr exc)
        : mStore{ std::move(exc) }
    {
    }

    inline async_error_info & async_error_info::operator=(std::error_code ec)
    {
        mStore = ec;
        return *this;
    }
    inline async_error_info & async_error_info::operator=(boost::exception_ptr exc)
    {
        mStore = exc;
        return *this;
    }
    inline async_error_info::operator bool() const
    {
        return which() != type::none;
    }
    inline async_error_info::type async_error_info::which() const
    {
        return type{ static_cast<int>(mStore.index()) };
    }
    inline bool async_error_info::is_code() const
    {
        return which() == type::code;
    }
    inline bool async_error_info::is_exception() const
    {
        return which() == type::exception;
    }
    inline const std::error_code & async_error_info::get_code() const
    {
        return std::get<std::error_code>(mStore);
    }
    inline const boost::exception_ptr & async_error_info::get_exception() const
    {
        return std::get<boost::exception_ptr>(mStore);
    }
}
