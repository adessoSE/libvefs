#pragma once

#include <cstddef>
#include <cstdint>

#include <type_traits>

namespace vefs
{
    using error_code = std::uintptr_t;

    class error;
    class error_info;
    class error_domain;

    template <typename Tag, typename T>
    class error_detail;

    enum class errc : error_code;

    enum class error_message_format
    {
        simple,
        with_diagnostics,
    };

    namespace adl::disappointment
    {
        struct type final {};
    }

    template <typename T, typename = void>
    struct is_error_compatible : std::false_type
    {
    };
    template <typename T>
    struct is_error_compatible<T,
        std::enable_if_t<
            std::is_same_v<decltype(make_error(std::declval<T>(), adl::disappointment::type{})), error>
        >
    > : std::true_type
    {
    };

    template <typename T>
    constexpr bool is_error_compatible_v = is_error_compatible<T>::value;

    namespace detail
    {
        constexpr std::size_t error_format_stack_buffer_size = 1024;
    }
}
