#pragma once

#include <tuple>
#include <string>
#include <type_traits>

#include <fmt/format.h>

#include <vefs/disappointment/fwd.hpp>

namespace vefs
{
    namespace detail
    {
        class error_detail_base
        {
        public:
            using format_buffer = fmt::basic_memory_buffer<char, error_format_stack_buffer_size>;

            error_detail_base() = default;
            virtual ~error_detail_base() noexcept = default;

            error_detail_base(const error_detail_base &) = delete;
            error_detail_base(error_detail_base &&) = delete;
            error_detail_base & operator=(const error_detail_base &) = delete;
            error_detail_base & operator=(error_detail_base &&) = delete;

            virtual void stringify(format_buffer &out) const noexcept = 0;
            virtual auto stringify() const
                -> std::string = 0;
        };
    }

    template <typename Tag, typename T>
    class error_detail final
        : public vefs::detail::error_detail_base
    {
        void stringify(format_buffer &out) const noexcept override;
        auto stringify() const
            -> std::string override;

    public:
        using value_type = T;

        error_detail() = delete;
        error_detail(error_detail &&other) noexcept(std::is_nothrow_move_constructible_v<T>);
        error_detail(const T &v) noexcept(std::is_nothrow_copy_constructible_v<T>);
        error_detail(T &&v) noexcept(std::is_nothrow_move_constructible_v<T>);

        auto value() noexcept
            -> value_type &;
        auto value() const noexcept
            -> const value_type &;

    private:
        value_type mValue;
    };

    template<typename Tag, typename T>
    inline error_detail<Tag, T>::error_detail(error_detail && other) noexcept(std::is_nothrow_move_constructible_v<T>)
        : error_detail(std::move(other.mValue))
    {
    }
    template<typename Tag, typename T>
    inline error_detail<Tag, T>::error_detail(const T & v) noexcept(std::is_nothrow_copy_constructible_v<T>)
        : mValue(v)
    {
    }
    template<typename Tag, typename T>
    inline error_detail<Tag, T>::error_detail(T && v) noexcept(std::is_nothrow_move_constructible_v<T>)
        : mValue(std::move(v))
    {
    }

    template<typename Tag, typename T>
    inline auto error_detail<Tag, T>::value() noexcept
        -> value_type &
    {
        return mValue;
    }
    template<typename Tag, typename T>
    inline auto error_detail<Tag, T>::value() const noexcept
        -> const value_type &
    {
        return mValue;
    }

    template<typename Tag, typename T>
    inline void error_detail<Tag, T>::stringify(format_buffer &out) const noexcept
    {
        auto start = out.size();
        try
        {
            std::string_view type{ typeid(Tag).name() };
            if (type.size() > 0)
            {
                fmt::format_to(out, "[{}] = ", type);
            }
            else
            {
                fmt::format_to(out, "unknown type: ");
            }
        }
        catch (...)
        {
            out.resize(start);
            try
            {
                fmt::format_to(out, "<type name format failed>: ");
            }
            catch (...)
            {
                // give up
                out.resize(start);
                return;
            }
        }

        auto valueStart = out.size();
        try
        {
            fmt::format_to(out, "{}", mValue);
            return; // success
        }
        catch (const std::exception &e)
        {
            out.resize(valueStart);
            try
            {
                std::string_view what{ e.what() };
                fmt::format_to(out, "<detail value format failed|{}>", what);
            }
            catch (...)
            {
                // give up
                out.resize(start);
            }
        }
        catch (...)
        {
        }

        out.resize(valueStart);
        try
        {
            fmt::format_to(out, "<detail value format failed>");
        }
        catch (...)
        {
            // give up
            out.resize(start);
        }
    }

    template <typename Tag, typename T>
    auto error_detail<Tag, T>::stringify() const
        -> std::string
    {
        std::string_view type{ typeid(Tag).name() };
        if (type.size() > 0)
        {
            return fmt::format(FMT_STRING("[{}] = {}"), type, mValue);
        }
        else
        {
            return fmt::format(FMT_STRING("unknown type: {}"), mValue);
        }
    }
}


namespace fmt
{
    template <>
    struct formatter<vefs::detail::error_detail_base>
    {
        template <typename ParseContext>
        constexpr auto parse(ParseContext &ctx)
        {
            return ctx.begin();
        }

        template <typename FormatContext>
        auto format(const vefs::detail::error_detail_base &detail, FormatContext &ctx)
        {
            return format_to(ctx.begin(), "{}", detail.stringify());
        }
    };
}
