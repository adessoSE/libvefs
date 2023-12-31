#pragma once

#ifdef __GNUC__
#include <cxxabi.h>
#endif

#include <string>
#include <typeinfo>

#include <boost/predef.h>

#include <fmt/format.h>

#if defined BOOST_COMP_GNUC_AVAILABLE || defined BOOST_COMP_CLANG_AVAILABLE

#define VEFS_PREFETCH_NTA(ptr) __builtin_prefetch((ptr), 0, 0)

#elif defined BOOST_COMP_MSVC_AVAILABLE

#if defined BOOST_ARCH_X86_AVAILABLE

#include <intrin.h>
#define VEFS_PREFETCH_NTA(ptr)                                                 \
    _mm_prefetch(reinterpret_cast<char const *>(ptr), _MM_HINT_NTA)

#elif defined BOOST_ARCH_ARM_AVAILABLE

#include <arm_neon.h>

#define VEFS_PREFETCH_NTA(ptr) __prefetch((ptr))

#else

#define VEFS_PREFETCH_NTA(ptr) ((void)ptr)

#endif

#else

#define VEFS_PREFETCH_NTA(ptr) ((void)ptr)

#endif

namespace vefs::utils
{
void set_current_thread_name(std::string const &name);
}

namespace vefs::detail
{
struct type_info_fmt
{
    std::type_info const &value;
};
} // namespace vefs::detail

namespace fmt
{

template <>
struct formatter<vefs::detail::type_info_fmt>
{
    constexpr auto parse(format_parse_context &ctx) noexcept
    {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(vefs::detail::type_info_fmt const &type, FormatContext &ctx)
    {
#ifdef __GNUC__
        char *demangledName;
        {
            int status = 0;
            demangledName
                    = abi::__cxa_demangle(type.value.name(), 0, 0, &status);
        }
#else
        auto demangledName = type.value.name();
#endif

        auto out = ctx.out();
        if (std::string_view demangledNameView{demangledName};
            demangledNameView.size() > 0)
        {
            out = fmt::format_to(out, FMT_STRING("{}"), demangledNameView);
        }
        else
        {
            using namespace std::string_view_literals;
            out = fmt::format_to(out, "<unknown type>"sv);
        }

#ifdef __GNUC__
        free(demangledName);
#endif

        return out;
    }
};

} // namespace fmt
