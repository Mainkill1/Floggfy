#pragma once
#include "settings_file.h"

namespace network {
// Match the controller and helper startup policy exactly. The return value says
// whether a readable file needs normalization; missing/unreadable files stay off.
inline bool ReadConfiguredMode(const std::wstring& path,int& mode) {
    mode=0;
    std::wstring text;bool unicode=false;
    if(!history::settings_file::Read(path,text,unicode))return false;
    const auto value=history::settings_file::Find(text,L"NetworkKillSwitch",L"[Misc]");
    if(value.present&&!value.duplicate) {
        if(value.raw_value==L"1")mode=1;
        else if(value.raw_value==L"0")mode=0;
        else if(value.raw_value==L"-1")mode=-1;
        else return true;
        return false;
    }
    return true;
}
} // namespace network
