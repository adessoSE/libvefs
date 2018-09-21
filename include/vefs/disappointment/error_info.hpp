#pragma once

#include <cstdint>

#include <new>
#include <memory>
#include <algorithm>
#include <typeindex>
#include <functional>
#include <type_traits>
#include <string_view>
#include <unordered_map>

#include <fmt/core.h>
#include <fmt/format.h>

#include <vefs/disappointment/error_detail.hpp>
#include <vefs/disappointment/error_domain.hpp>

namespace vefs
{
    class error_info;

    template <typename T, typename = void>
    struct is_error_code_enum : std::false_type
    {
    };
    template <typename T>
    struct is_error_code_enum<T,
        std::enable_if_t<
            std::is_same_v<decltype(make_error_info(std::declval<T>())), error_info>
        >
    >   : public std::true_type
    {
    };

    template <typename T>
    constexpr bool is_error_code_enum_v = is_error_code_enum<T>::value;

    class error_info final
    {
        class success_domain_t final
            : public error_domain
        {
            virtual std::string_view name() const noexcept override;
            virtual std::string_view message(intptr_t value) const noexcept override;
        };
        static constexpr success_domain_t success_domain_impl{};

        struct additional_details
        {
            using detail_ptr = std::unique_ptr<detail::error_detail_base>;
            using map_t = std::unordered_map<std::type_index, detail_ptr>;

            map_t mDetails;
            int mInsertionFailures;
        };
        using additional_details_ptr = std::shared_ptr<additional_details>;

    public:
        using value_type = intptr_t;

        constexpr error_info() noexcept;
        constexpr error_info(value_type code, const error_domain &domain) noexcept;
        template <typename T, std::enable_if_t<is_error_code_enum_v<T>, int> = 0>
        constexpr error_info(T code) noexcept(noexcept(make_error_info(std::declval<T>())))
            : error_info{ make_error_info(code) }
        {
        }

        constexpr explicit operator bool() const noexcept;

        constexpr auto value() const noexcept
            ->value_type;
        constexpr auto domain() const noexcept
            -> const error_domain &;

        template <typename ErrorInfo>
        auto try_get() const
            ->std::shared_ptr<const typename ErrorInfo::value_type>;

        auto diagnostic_information(bool verbose = true) const
            -> std::string;

        template <typename Tag, typename T>
        friend auto operator<<(error_info &ei, error_detail<Tag, T> &&det) noexcept
            -> error_info &;

    private:
        value_type mValue;
        const error_domain *mDomain;
        additional_details_ptr mAD;
    };

    constexpr error_info::error_info() noexcept
        : error_info{ static_cast<value_type>(0), success_domain_impl }
    {
    }
    constexpr error_info::error_info(value_type code, const error_domain &domain) noexcept
        : mValue{ code }
        , mDomain{ &domain }
        , mAD{}
    {
    }

    constexpr error_info::operator bool() const noexcept
    {
        return *mDomain == success_domain_impl;
    }

    constexpr auto error_info::value() const noexcept
        -> value_type
    {
        return mValue;
    }
    constexpr auto error_info::domain() const noexcept
        -> const error_domain &
    {
        return *mDomain;
    }

    template<typename ErrorInfo>
    inline auto error_info::try_get() const
        -> std::shared_ptr<const typename ErrorInfo::value_type>
    {
        if (!mAD)
        {
            return std::nullopt;
        }
        auto it = mAD->mDetails.find(typeid(ErrorInfo));
        if (it == mAD->mDetails.cend())
        {
            return std::nullopt;
        }
        return { mAD, &static_cast<const ErrorInfo *>(it->second)->value() };
    }

    constexpr bool operator==(const error_info &lhs, const error_info &rhs)
    {
        return lhs.domain() == rhs.domain()
            && lhs.value() == rhs.value();
    }
    constexpr bool operator!=(const error_info &lhs, const error_info &rhs)
    {
        return !(lhs == rhs);
    }

    template <typename Tag, typename T>
    auto operator<<(error_info &ei, error_detail<Tag, T> &&det) noexcept
        -> error_info &
    {
        using detail_type = error_detail<Tag, T>;
        using detail_ptr = error_info::additional_details::detail_ptr;
        if (!ei.mAD)
        {
            try
            {
                ei.mAD = std::make_shared<error_info::additional_details>();
            }
            catch (const std::bad_alloc &)
            {
                // #TODO #OOM maybe use a preallocated pool for emergency cases
                std::terminate();
            }
        }
        auto &ad = ei.mAD;
        try
        {
            detail_ptr hdet{ new detail_type{ std::move(det) } };
            auto erx = ad->mDetails.try_emplace(typeid(detail_type), std::move(hdet));

            ad->mInsertionFailures += !std::get<bool>(erx);
        }
        catch (const std::bad_alloc &)
        {
            ++ad->mInsertionFailures;
        }

        return *this;
    }

    template <typename T>
    error_info make_error_info(T code) noexcept = delete;
}

namespace fmt
{
    template<>
    class formatter<vefs::error_info>
    {
        using buffer = internal::buffer;

        constexpr auto parse(string_view ctx)
            -> const char *
        {
            using namespace std::string_view_literals;

            if (!ctx.size())
            {
                return ctx.data();
            }
            std::string_view part{ ctx.data(), ctx.size() };
            const bool px = part[0] == ':';
            auto xend = part.find('}');
            part = part.substr(px, xend - px);

            if (!part.size())
            {
                return ctx.data();
            }

            if (part == "!v"sv)
            {
                verbose = false;
            }
            else if (part == "v"sv)
            {
                verbose = true;
            }
            return ctx.data() + xend;
        }

        template <typename Buffer, typename ParseContext>
        void format(Buffer &buf, const vefs::error_info &info, const ParseContext &)
        {
            auto str = info.diagnostic_information(verbose);
            buf.append(str.cbegin(), str.cend());
        }

        bool verbose = true;
    };
}
