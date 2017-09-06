#include "precompiled.hpp"
#include "napi_.hpp"

#include <fileformat.pb.h>

void Init(Napi::Env env, Napi::Object exports, [[maybe_unused]] Napi::Object module)
{
    adesso::vefs::UnsafeFileHeader header;
    exports.Set(Napi::String::New(env, "hello"), Napi::String::New(env, "world"));
}


#if defined _MSC_VER
#pragma warning(push, 3)
#endif

NODE_API_MODULE(vefs, Init)

#if defined _MSC_VER
#pragma warning(pop)
#endif
