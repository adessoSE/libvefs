
#include <napi.h>
#include <uv.h>

void Init(Napi::Env env, Napi::Object exports, Napi::Object module)
{
    exports.Set(Napi::String::New(env, "hello"), Napi::String::New(env, "world"));
}

NODE_API_MODULE(vefs, Init)
