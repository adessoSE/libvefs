#pragma once

#include <cstdint>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <new>
#include <ostream>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>

#include <fmt/core.h>
#include <fmt/format.h>

#include <vefs/disappointment/error_detail.hpp>
#include <vefs/disappointment/error_domain.hpp>
#include <vefs/disappointment/fwd.hpp>
#include <vefs/disappointment/generic_errc.hpp>
#include <vefs/utils/ref_ptr.hpp>

namespace vefs
{
class error_info final
{
    using detail_ptr = std::unique_ptr<detail::error_detail_base>;
    using detail_map_t = std::unordered_map<std::type_index, detail_ptr>;

public:
    using ptr = utils::ref_ptr<error_info>;
    using diagnostics_buffer = detail::error_detail_base::format_buffer;

    error_info() noexcept;
    error_info(error_info const &) = delete;

    error_info &operator=(error_info &) = delete;

    template <typename ErrorDetail>
    auto detail() const
            -> utils::aliasing_ref_ptr<const typename ErrorDetail::value_type,
                                       const error_info>;
    template <typename ErrorDetail>
    auto try_add_detail(ErrorDetail &&detail) noexcept -> vefs::error;
    auto try_add_detail(std::type_index type, detail_ptr ptr) noexcept
            -> vefs::error;

    void diagnostic_information(diagnostics_buffer &out,
                                std::string_view detailFormat) const;

    void add_reference() const noexcept;
    void release() const noexcept;

private:
    ~error_info() /* = default (but no inline)*/;

    detail_map_t mDetails;

    mutable std::atomic_int mRefCtr{1};
    int mInsertionFailures{0};
};
static_assert(!std::is_default_constructible_v<error_info>);
static_assert(!std::is_copy_constructible_v<error_info>);
static_assert(!std::is_move_constructible_v<error_info>);
static_assert(!std::is_copy_assignable_v<error_info>);
static_assert(!std::is_move_assignable_v<error_info>);

template <typename ErrorDetail>
inline auto error_info::detail() const
        -> utils::aliasing_ref_ptr<const typename ErrorDetail::value_type,
                                   const error_info>
{
    if (auto it = mDetails.find(typeid(ErrorDetail)); it != mDetails.end())
    {
        return {
                &static_cast<ErrorDetail *>(it->second.get())->value(),
                utils::ref_ptr{this, utils::ref_ptr_acquire}
        };
    }
    else
    {
        return nullptr;
    }
}

inline void
error_info::diagnostic_information(diagnostics_buffer &out,
                                   std::string_view detailFormat) const
{
    fmt::string_view wrappedFormat{detailFormat};
    for (auto const &detail : mDetails)
    {
        fmt::format_to(std::back_inserter(out), "{}", wrappedFormat);
        detail.second->stringify(out);
    }
}

inline void error_info::add_reference() const noexcept
{
    mRefCtr.fetch_add(1, std::memory_order_relaxed);
}
inline void error_info::release() const noexcept
{
    if (mRefCtr.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        delete this;
    }
}

class error final
{
    class success_domain final : public error_domain
    {
        auto name() const noexcept -> std::string_view override;
        auto message(error const &e, const error_code code) const noexcept
                -> std::string_view override;

        static const success_domain sInstance;

    public:
        static auto instance() noexcept -> error_domain const &;
    };

public:
    error() noexcept;
    error(error_code code,
          error_domain const &domain,
          error_info::ptr info = {}) noexcept;
    template <typename T, std::enable_if_t<is_error_compatible_v<T>, int> = 0>
    error(T code) noexcept(noexcept(make_error(code,
                                               adl::disappointment::type{})))
        : error{make_error(code, adl::disappointment::type{})}
    {
    }

    error(error const &other) noexcept;
    error(error &&other) noexcept;
    auto operator=(error const &other) noexcept -> error &;
    auto operator=(error &&other) noexcept -> error &;

    auto code() const noexcept -> error_code;
    auto domain() const noexcept -> error_domain const &;

    bool has_info() const noexcept;
    auto info() const noexcept -> error_info::ptr const &;
    auto ensure_allocated() const noexcept -> error;

    template <typename T>
    auto detail() const noexcept;

    auto diagnostic_information(error_message_format format) const noexcept
            -> std::string;

    explicit operator bool() const noexcept;
    friend bool operator==(error const &lhs, error const &rhs) noexcept;

private:
    error_code mValue;
    mutable error_info::ptr mInfo;
    error_domain const *mDomain;
};

template <typename T>
auto make_error(...) noexcept -> error = delete;

inline auto make_error(errc code, adl::disappointment::type) noexcept -> error
{
    return error{static_cast<error_code>(code), generic_domain()};
}
template <>
struct is_error_compatible<errc> : std::true_type
{
};

inline error::error() noexcept
    : mValue{}
    , mInfo{nullptr}
    , mDomain{nullptr}
{
}
inline error::error(error_code code,
                    error_domain const &domain,
                    error_info::ptr info) noexcept
    : mValue{code}
    , mInfo{std::move(info)}
    , mDomain{&domain}

{
}

inline error::error(error const &other) noexcept
    : mValue{other.mValue}
    , mInfo{other.mInfo}
    , mDomain{other.mDomain}
{
}

inline error::error(error &&other) noexcept
    : mValue{std::exchange(other.mValue, 0)}
    , mInfo{std::exchange(other.mInfo, nullptr)}
    , mDomain{std::exchange(other.mDomain, nullptr)}
{
}

inline error &error::operator=(error const &other) noexcept
{
    mDomain = other.mDomain;
    mValue = other.mValue;
    mInfo = other.mInfo;
    return *this;
}

inline error &error::operator=(error &&other) noexcept
{
    mValue = std::exchange(other.mValue, 0);
    mInfo = std::exchange(other.mInfo, nullptr);
    mDomain = std::exchange(other.mDomain, nullptr);
    return *this;
}

template <typename ErrorDetail>
inline auto error_info::try_add_detail(ErrorDetail &&detail) noexcept
        -> vefs::error
{
    static_assert(
            std::is_convertible_v<std::add_pointer_t<ErrorDetail>,
                                  vefs::detail::error_detail_base const *>,
            "The detail type must derive from "
            "vefs::detail::error_detail_base.");
    using obj_type = std::remove_cv_t<std::remove_reference_t<ErrorDetail>>;

    detail_ptr heapDetail;

    if constexpr (noexcept(obj_type(std::forward<ErrorDetail>(detail))))
    {
        heapDetail.reset(new (std::nothrow)
                                 obj_type(std::forward<ErrorDetail>(detail)));
    }
    else
    {
        try
        {
            heapDetail.reset(new (std::nothrow) obj_type(
                    std::forward<ErrorDetail>(detail)));
        }
        catch (std::bad_alloc const &)
        {
            // fall through to !heapDetail
        }
        catch (...)
        {
            return errc::user_object_copy_failed;
        }
    }

    if (!heapDetail)
    {
        ++mInsertionFailures;
        return errc::not_enough_memory;
    }
    return try_add_detail(typeid(ErrorDetail), std::move(heapDetail));
}

inline auto error_info::try_add_detail(std::type_index type,
                                       detail_ptr detail) noexcept
        -> vefs::error
{
    try
    {
        if (auto [it, inserted] = mDetails.emplace(type, std::move(detail));
            !inserted)
        {
            (void)it;
            return errc::key_already_exists;
        }
    }
    catch (std::bad_alloc const &)
    {
        ++mInsertionFailures;
        return errc::not_enough_memory;
    }
    catch (std::exception const &)
    {
        ++mInsertionFailures;
        return errc::bad;
    }
    return error{};
}

inline auto error::code() const noexcept -> error_code
{
    return mValue;
}

inline auto error::domain() const noexcept -> error_domain const &
{
    return mDomain ? *mDomain : success_domain::instance();
}

inline bool error::has_info() const noexcept
{
    return mInfo.operator bool();
}

inline auto error::info() const noexcept -> error_info::ptr const &
{
    return mInfo;
}

inline auto error::ensure_allocated() const noexcept -> error
{
    if (!has_info())
    {
        mInfo = utils::ref_ptr{new (std::nothrow) error_info(),
                               utils::ref_ptr_import};
        if (!mInfo)
        {
            return errc::not_enough_memory;
        }
    }
    return error{};
}

template <typename T>
inline auto error::detail() const noexcept
{
    using result_t = decltype(info()->detail<T>());
    if (has_info())
    {
        return info()->detail<T>();
    }
    else
    {
        return result_t{};
    }
}

inline error::operator bool() const noexcept
{
    return mDomain != nullptr;
}

inline bool operator==(error const &lhs, error const &rhs) noexcept
{
    return lhs.mDomain == rhs.mDomain && lhs.code() == rhs.code();
}
inline bool operator!=(error const &lhs, error const &rhs) noexcept
{
    return !(lhs == rhs);
}

template <typename T>
auto operator<<(error &e, T &&detail) noexcept -> error &
{
    // !error
    if (!e.ensure_allocated())
    {
        e.info()->try_add_detail(std::forward<T>(detail));
    }
    return e;
}
template <typename T>
auto operator<<(error &&e, T &&detail) noexcept -> error &&
{
    // !error
    if (!e.ensure_allocated())
    {
        e.info()->try_add_detail(std::forward<T>(detail));
    }
    return std::move(e);
}
} // namespace vefs

namespace fmt
{
template <>
class formatter<vefs::error>
{
public:
    template <typename ParseContext>
    constexpr auto parse(ParseContext &ctx)
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
            auto const val = *xit;
            switch (state)
            {
            case 0: // start-state
                switch (val)
                {
                case ':': // this should actually lead to a unique state but I'm
                          // lazy
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
                    error_format = vefs::error_message_format::with_diagnostics;
                    break;

                default:
                    ctx.on_error(errfmt);
                    break;
                }
                break;

            case 1: // "!" was parsed
                if (val != 'v')
                {
                    ctx.on_error(errfmt);
                }
                state = 2;
                error_format = vefs::error_message_format::simple;
                break;

            case 2: // "v" was parsed
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
    auto format(vefs::error const &info, FormatContext &ctx)
    {
        auto str = info.diagnostic_information(error_format);
        return std::copy(str.cbegin(), str.cend(), ctx.out());
    }

    vefs::error_message_format error_format
            = vefs::error_message_format::with_diagnostics;
};
} // namespace fmt

namespace vefs
{
inline auto operator<<(std::ostream &s, error const &e) -> std::ostream &
{
    s << fmt::format(FMT_STRING("{}"), e);
    return s;
}
} // namespace vefs