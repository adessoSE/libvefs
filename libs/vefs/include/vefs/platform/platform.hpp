#pragma once

#include <string>

#include <boost/predef.h>

#if defined BOOST_COMP_GNUC_AVAILABLE || defined BOOST_COMP_CLANG_AVAILABLE

#define VEFS_PREFETCH_NTA(ptr) __builtin_prefetch((ptr), 0, 0)

#elif defined BOOST_COMP_MSVC_AVAILABLE

#if defined BOOST_ARCH_X86_AVAILABLE

#include <intrin.h>
#define VEFS_PREFETCH_NTA(ptr) _mm_prefetch(reinterpret_cast<const char *>(ptr), _MM_HINT_NTA)

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
    void set_current_thread_name(const std::string &name);
}
