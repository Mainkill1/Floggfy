#pragma once
#include <shlobj.h>
#include <memory>
#include <string>

namespace history {
inline HRESULT ReadKnownFolder(REFKNOWNFOLDERID id, std::wstring& path) {
    PWSTR raw = nullptr;
    const auto status = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw);
    const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> folder(raw, &CoTaskMemFree);
    if (SUCCEEDED(status)) path.assign(folder.get());
    return status;
}
}
