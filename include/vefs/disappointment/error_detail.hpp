#pragma once

#include <tuple>
#include <string>
#include <type_traits>

#include <fmt/format.h>

namespace vefs
{
    namespace detail
    {
        class error_detail_base
        {
        public:
            error_detail_base() = default;
            virtual ~error_detail_base() noexcept = default;

            error_detail_base(const error_detail_base &) = delete;
            error_detail_base(error_detail_base &&) = delete;
            error_detail_base & operator=(const error_detail_base &) = delete;
            error_detail_base & operator=(error_detail_base &&) = delete;

            virtual auto stringify() const noexcept
                -> std::tuple<std::string, bool> = 0;
        };
    }

    template <typename Tag, typename T>
    class error_detail final
        : public detail::error_detail_base
    {
        virtual auto stringify() const noexcept
            -> std::tuple<std::string, bool> override;

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

    template <typename Tag, typename T>
    auto error_detail<Tag, T>::stringify() const noexcept
        -> std::tuple<std::string, bool>
    {
        try
        {
            using namespace fmt::literals;
            std::string_view type{ typeid(error_detail<Tag, T>).name() };
            if (type.size() > 0)
            {
                return "{{{}}}: {}"_format(type, mValue);
            }
            else
            {
                return "unknown type: {}"_format(mValue);
            }
        }
        catch (...)
        {
            return std::nullopt;
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
            auto optstr = detail.stringify();
            if (!optstr)
            {
                throw std::bad_alloc{};
            }
            return format_to(ctx.begin(), "{}", optstr);
        }
    };
}
