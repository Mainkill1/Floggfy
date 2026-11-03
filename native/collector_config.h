#pragma once
#include "history_settings.h"
#include "auto_skip.h"

namespace history {
inline std::wstring BuildCollectorOptions(const Settings& settings,const AutoSkipRequest& request={}) {
    const bool debug=DiagnosticsEnabled(settings.log,settings.debug_log);
    const bool connect=debug&&settings.debug_connect;
    const bool networking=debug&&settings.debug_networking;
    const bool auto_skip=settings.auto_skip_downloaded&&request.token&&
        request.settings_generation==settings.generation;
    std::wstring command;
    if(auto_skip) {
        command=L",skipToken:"+std::to_wstring(request.token)+L",skipUri:'";
        command.append(request.uri.begin(),request.uri.end());command+=L"'";
    }
    return std::wstring(L"window.__floggfyConfig={metadata:")+(settings.metadata?L"true":L"false")+
        L",lyrics:"+(settings.lyrics?L"true":L"false")+
        L",autoSkip:"+(settings.auto_skip_downloaded?L"true":L"false")+command+
        L",debug:"+(debug?L"true":L"false")+
        L",connect:"+(connect?L"true":L"false")+
        L",networking:"+(networking?L"true":L"false")+L"};";
}
}
