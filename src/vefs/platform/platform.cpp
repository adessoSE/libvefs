#include <vefs/platform/platform.hpp>

#include <boost/predef.h>

#if defined BOOST_OS_WINDOWS_AVAILABLE

#include "windows-proper.h"

namespace vefs::utils
{
namespace
{
using set_native_thread_name_fn = HRESULT (*)(HANDLE hThread, PCWSTR name);

set_native_thread_name_fn set_native_thread_name()
{
    static set_native_thread_name_fn fn = []() -> set_native_thread_name_fn {
        set_native_thread_name_fn dfn = nullptr;
        if (auto dll = LoadLibraryW(L"Kernel32.dll"))
        {
            dfn = reinterpret_cast<set_native_thread_name_fn>(
                    GetProcAddress(dll, "SetThreadDescription"));
            FreeLibrary(dll);
        }
        return dfn;
    }();
    return fn;
}

using set_thread_name_fn = void (*)(std::string const &name);

void set_current_thread_name_spicey(std::string const &name)
{
    auto wsize = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
    assert(wsize > 0);

    std::wstring buffer(static_cast<std::size_t>(wsize), L'\0');

    MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, buffer.data(),
                        static_cast<int>(buffer.size()));

    // we can use data instead of c_str, because MBTWC wrote a null terminator
    // explicitly into buffer.
    set_native_thread_name()(GetCurrentThread(), buffer.data());
}

#pragma pack(push, 8)

void set_current_thread_name_legacy(std::string const &name)
{
    // adapted from
    // https://docs.microsoft.com/en-us/visualstudio/debugger/how-to-set-a-thread-name-in-native-code

    constexpr DWORD MS_VC_EXCEPTION = 0x406D'1388;

    struct THREADNAME_INFO
    {
        DWORD dwType;     // Must be 0x1000.
        LPCSTR szName;    // Pointer to name (in user addr space).
        DWORD dwThreadID; // Thread ID (-1=caller thread).
        DWORD dwFlags;    // Reserved for future use, must be zero.
    } info{0x1000, name.c_str(), static_cast<DWORD>(-1), 0};

    static_assert(std::is_standard_layout_v<decltype(info)>);
    static_assert(std::is_trivial_v<decltype(info)>);

#pragma warning(push)
#pragma warning(disable : 6320 6322)
    __try
    {
        RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR),
                       (ULONG_PTR *)&info);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
#pragma warning(pop)
}

#pragma pack(pop)

} // namespace

void set_current_thread_name(std::string const &name)
{
    static set_thread_name_fn impl = set_native_thread_name()
                                             ? &set_current_thread_name_spicey
                                             : &set_current_thread_name_legacy;

    impl(name);
}
} // namespace vefs::utils

#elif defined BOOST_OS_LINUX_AVAILABLE

#include <pthread.h>

namespace vefs::utils
{
void set_current_thread_name(std::string const &name)
{
    auto const id = pthread_self();
    pthread_setname_np(id, name.c_str());
}
} // namespace vefs::utils

#elif defined BOOST_OS_MACOS_AVAILABLE

#include <pthread.h>

namespace vefs::utils
{
void set_current_thread_name(std::string const &name)
{
    pthread_setname_np(name.c_str());
}
} // namespace vefs::utils

#else

namespace vefs::utils
{
void set_current_thread_name(std::string const &name)
{
}
} // namespace vefs::utils

#endif
