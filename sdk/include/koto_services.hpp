#pragma once
#include "module_abi.h"
#include <algorithm>
#include <string>
#include <vector>

// Host 74+: JSON strings keep future named services independent of ABI layout.
// Set module.min_host_version=74, then discover methods with capabilities().
namespace koto::services {
inline bool available(koto_ctx* ctx,const koto_api* api) {
    return api && api->host_version && api->host_version(ctx)>=74;
}
inline std::string call(koto_ctx* ctx,const koto_api* api,const char* method,const std::string& request="{}") {
    if (!available(ctx,api) || !api->host_call_json)
        return R"({"schema":1,"ok":false,"error":"host_too_old"})";
    std::vector<char> buffer(4096);
    int size=api->host_call_json(ctx,method,request.c_str(),buffer.data(),static_cast<int>(buffer.size()));
    if (size>=static_cast<int>(buffer.size()) && size<=256*1024) {
        buffer.resize(static_cast<size_t>(size)+1);
        size=api->host_call_json(ctx,method,request.c_str(),buffer.data(),static_cast<int>(buffer.size()));
    }
    if (size<0 || size>=static_cast<int>(buffer.size()))
        return R"({"schema":1,"ok":false,"error":"invalid_host_response"})";
    return std::string(buffer.data(),static_cast<size_t>(size));
}
inline std::string capabilities(koto_ctx* ctx,const koto_api* api) {return call(ctx,api,"capabilities");}
// true means queued. Telegram confirmation/failure is logged by the host.
inline bool action(koto_ctx* ctx,const koto_api* api,const char* method,const std::string& request) {
    return available(ctx,api) && api->host_action_json && api->host_action_json(ctx,method,request.c_str())==1;
}
} // namespace koto::services
