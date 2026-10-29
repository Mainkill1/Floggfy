#pragma once
#include "history_settings.h"

namespace history {
inline std::wstring BuildCollectorOptions(const Settings& settings) {
    const bool debug=DiagnosticsEnabled(settings.log,settings.debug_log);
    const bool connect=debug&&settings.debug_connect;
    const bool networking=debug&&settings.debug_networking;
    return std::wstring(L"window.__floggfyConfig={metadata:")+(settings.metadata?L"true":L"false")+
        L",lyrics:"+(settings.lyrics?L"true":L"false")+
        L",debug:"+(debug?L"true":L"false")+
        L",connect:"+(connect?L"true":L"false")+
        L",networking:"+(networking?L"true":L"false")+L"};";
}
}
