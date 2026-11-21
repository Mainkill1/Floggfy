#pragma once
#include "settings_file.h"

namespace network {
// Match the controller and helper startup policy exactly. The return value says
// whether a readable file needs normalization; missing/unreadable files stay off.
inline bool ReadConfiguredMode(const history::settings_file::Snapshot& file,int& mode) {
    const auto value=history::settings_file::Find(file.text,L"NetworkKillSwitch",L"[Misc]");
    return file.ReadChoice(value,mode,0,{-1,0,1});
}
inline bool ReadConfiguredMode(const std::wstring& path,int& mode) {
    return ReadConfiguredMode(history::settings_file::Snapshot(path),mode);
}
} // namespace network
