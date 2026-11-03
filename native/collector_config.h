#pragma once
#include "history_settings.h"
#include "auto_skip.h"

namespace history {
inline void AppendCollectorString(std::wstring& output,const std::string& value) {
    constexpr wchar_t hex[]=L"0123456789abcdef";
    for(const unsigned char c:value) {
        if(c==L'\\'||c==L'\''){output+=L'\\';output+=wchar_t(c);}
        else if(c>=0x20&&c<=0x7e)output+=wchar_t(c);
        else {output+=L"\\x";output+=hex[c>>4];output+=hex[c&15];}
    }
}
inline std::wstring BuildCollectorOptions(const Settings& settings,const AutoSkipRequest& request={}) {
    const bool debug=settings.log_level>=4;
    const bool connect=debug&&settings.debug_connect;
    const bool networking=debug&&settings.debug_networking;
    const bool auto_skip=settings.auto_skip_downloaded&&request.token&&
        request.settings_generation==settings.generation;
    std::wstring command;
    if(auto_skip) {
        command=L",skipToken:"+std::to_wstring(request.token)+L",skipUri:'";
        AppendCollectorString(command,request.uri);command+=L"',skipInstance:'";
        AppendCollectorString(command,request.playback_instance);command+=L"'";
    }
    return std::wstring(L"window.__floggfyConfig={metadata:")+(settings.metadata?L"true":L"false")+
        L",lyrics:"+(settings.lyrics?L"true":L"false")+
        L",autoSkip:"+(settings.auto_skip_downloaded?L"true":L"false")+command+
        L",debug:"+(debug?L"true":L"false")+
        L",connect:"+(connect?L"true":L"false")+
        L",networking:"+(networking?L"true":L"false")+L"};";
}
}
