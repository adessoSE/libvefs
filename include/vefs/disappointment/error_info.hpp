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

    class error_info2
    {
    public:
        raw_error_value xval;
    };

    class error final
    {
        class success_domain_t final
            : public error_domain2
        {
            virtual auto name() const noexcept
                -> std::string_view;
            virtual auto message(raw_error_value handle, bool verbose) const noexcept
                -> std::string_view;
        };
        static constexpr success_domain_t success_domain{};

    public:
        error() noexcept;
        error(raw_error_value code, const error_domain2 &domain) noexcept;
        error(std::unique_ptr<error_info2> complex, const error_domain2 &domain) noexcept;
        template <typename T, std::enable_if_t<is_error_code_enum_v<T>, int> = 0>
        error(T code) noexcept(noexcept(make_error_info(code)))
            : error_info{ make_error_info(code) }
        {
        }

        ~error() noexcept;

        auto code() const noexcept
            -> raw_error_value;
        auto domain() const noexcept
            -> const error_domain2 &;

        bool has_info() const noexcept;
        auto info() noexcept
            -> error_info2 &;
        auto info() const noexcept
            -> const error_info2 &;
        template <typename T>
        auto info_as()
            -> T &
        {
            return static_cast<T &>(info())
        }
        template <typename T>
        auto info_as() const
            -> const T &
        {
            return static_cast<const T &>(info());
        }

        explicit operator bool() const noexcept;



    private:
        static constexpr auto wrap_simple(raw_error_value value) noexcept
            -> raw_error_value;
        static constexpr auto unwrap_simple(raw_error_value wrapped) noexcept
            -> raw_error_value;

        const error_domain2 *mDomain;
        raw_error_value mValue;
    };



    inline error::error() noexcept
        : error{ raw_error_value{}, success_domain }
    {
    }
    inline vefs::error::error(raw_error_value code, const error_domain2 &domain) noexcept
        : mDomain{ &domain }
        , mValue{ wrap_simple(code) }
    {
    }
    inline error::error(std::unique_ptr<error_info2> complex, const error_domain2 & domain) noexcept
        : mDomain{ &domain }
        , mValue{ reinterpret_cast<raw_error_value>(complex.release()) }
    {
        assert(has_info());
    }

    inline error::~error() noexcept
    {
        if (has_info())
        {
            delete reinterpret_cast<error_info2 *>(mValue);
        }
    }

    inline auto error::code() const noexcept
        -> raw_error_value
    {
        if (has_info())
        {
            return info().xval;
        }
        else
        {
            return unwrap_simple(mValue);
        }
        return raw_error_value();
    }

    inline auto error::domain() const noexcept
        -> const error_domain2 &
    {
        return *mDomain;
    }

    inline bool error::has_info() const noexcept
    {
        return (mValue & std::numeric_limits<raw_error_value>::max() - 1) == mValue;
    }

    inline auto error::info() noexcept
        -> error_info2 &
    {
        return *reinterpret_cast<error_info2 *>(mValue);
    }

    inline auto error::info() const noexcept
        -> const error_info2 &
    {
        return *reinterpret_cast<const error_info2 *>(mValue);
    }

    inline error::operator bool() const noexcept
    {
        return *mDomain != success_domain;
    }

    constexpr auto error::wrap_simple(raw_error_value value) noexcept
        -> raw_error_value
    {
        return (value << 1) | 1;
    }

    constexpr auto error::unwrap_simple(raw_error_value wrapped) noexcept
        -> raw_error_value
    {
        return wrapped >> 1;
    }


    class error_info final
    {
        class success_domain_t final
            : public error_domain
        {
            std::string_view name() const noexcept override;
            std::string_view message(intptr_t value) const noexcept override;
        };
        static constexpr success_domain_t success_domain_impl{};

        struct additional_details
        {
            using detail_ptr = std::unique_ptr<detail::error_detail_base>;
            using map_t = std::unordered_map<std::type_index, detail_ptr>;

            map_t mDetails;
            int mInsertionFailures{ 0 };
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
        : error_info{ value_type{0}, success_domain_impl }
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

        return ei;
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

    public:
        template <typename ParseContext>
        constexpr auto parse(ParseContext &ctx)
            -> const char *
        {
            constexpr auto errfmt = "invalid error_info formatter";

            int state = 0;
            auto xbegin = ctx.begin();
            auto xit = xbegin;
            auto xend = ctx.end();

            // I would prefer a string_view based solution any time,
            // but sadly the compiler/library support for constexpr string_views
            // is rather scarce

            while (xit != xend)
            {
                const auto val = *xit;
                switch (state)
                {
                case 0:
                    switch (val)
                    {
                    case ':': // this should actually lead to a unique state but I'm lazy
                        if (xit != xbegin)
                        {
                            ctx.on_error(errfmt);
                        }
                        break;

                    case '}':
                        xend = xit;
                        continue;

                    case '!':
                        state = 1;
                        break;

                    case 'v':
                        state = 2;
                        verbose = true;
                        break;

                    default:
                        ctx.on_error(errfmt);
                        break;
                    }
                    break;

                case 1:
                    if (val != 'v')
                    {
                        ctx.on_error(errfmt);
                    }
                    state = 2;
                    verbose = false;
                    break;

                case 2:
                    if (val != '}')
                    {
                        ctx.on_error(errfmt);
                    }
                    xend = xit;
                    continue;
                }
                ++xit;
            }

            return xend;
        }

        template <typename FormatContext>
        auto format(const vefs::error_info &info, FormatContext &ctx)
            -> decltype(ctx.out())
        {
            auto str = info.diagnostic_information(verbose);
            return std::copy(str.cbegin(), str.cend(), ctx.out());
        }

        bool verbose = true;
    };
}
