#pragma once

#include <string>
#include <tuple>
#include <type_traits>
#ifdef __GNUC__
#include <cxxabi.h>
#endif

#include <fmt/format.h>

#include <vefs/disappointment/fwd.hpp>
#include <vefs/platform/platform.hpp>

namespace vefs
{
namespace detail
{
class error_detail_base
{
public:
    using format_buffer
            = fmt::basic_memory_buffer<char, error_format_stack_buffer_size>;

    error_detail_base() = default;
    virtual ~error_detail_base() noexcept = default;

    error_detail_base(error_detail_base const &) = delete;
    error_detail_base(error_detail_base &&) = delete;
    error_detail_base &operator=(error_detail_base const &) = delete;
    error_detail_base &operator=(error_detail_base &&) = delete;

    virtual void stringify(format_buffer &out) const noexcept = 0;
    [[nodiscard]] virtual auto stringify() const -> std::string = 0;
};
} // namespace detail

template <typename Tag, typename T>
class error_detail final : public vefs::detail::error_detail_base
{
    void stringify(format_buffer &out) const noexcept override;
    [[nodiscard]] auto stringify() const -> std::string override;

public:
    using value_type = T;

    error_detail() = delete;
    error_detail(error_detail &&other) noexcept(
            std::is_nothrow_move_constructible_v<T>);
    error_detail(T const &v) noexcept(std::is_nothrow_copy_constructible_v<T>);
    error_detail(T &&v) noexcept(std::is_nothrow_move_constructible_v<T>);

    auto value() noexcept -> value_type &;
    [[nodiscard]] auto value() const noexcept -> value_type const &;

private:
    value_type mValue;
};

template <typename Tag, typename T>
inline error_detail<Tag, T>::error_detail(error_detail &&other) noexcept(
        std::is_nothrow_move_constructible_v<T>)
    : error_detail(std::move(other.mValue))
{
}
template <typename Tag, typename T>
inline error_detail<Tag, T>::error_detail(T const &v) noexcept(
        std::is_nothrow_copy_constructible_v<T>)
    : mValue(v)
{
}
template <typename Tag, typename T>
inline error_detail<Tag, T>::error_detail(T &&v) noexcept(
        std::is_nothrow_move_constructible_v<T>)
    : mValue(std::move(v))
{
}

template <typename Tag, typename T>
inline auto error_detail<Tag, T>::value() noexcept -> value_type &
{
    return mValue;
}
template <typename Tag, typename T>
inline auto error_detail<Tag, T>::value() const noexcept -> value_type const &
{
    return mValue;
}

template <typename Tag, typename T>
inline void error_detail<Tag, T>::stringify(format_buffer &out) const noexcept
{
    using namespace std::string_view_literals;
    fmt::format_to(std::back_inserter(out), "no longer implemented"sv);
}

template <typename Tag, typename T>
auto error_detail<Tag, T>::stringify() const -> std::string
{
    using namespace std::string_literals;
    return "no longer implemented"s;
}
} // namespace vefs

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
    auto format(vefs::detail::error_detail_base const &detail,
                FormatContext &ctx)
    {
        return format_to(ctx.begin(), "{}", detail.stringify());
    }
};
} // namespace fmt
