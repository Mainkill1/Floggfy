#pragma once
#include <windows.h>
#include <string>

namespace win32 {
inline bool PathMissing(DWORD error) noexcept {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

inline bool IsFile(DWORD attributes) noexcept {
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}
}

namespace history {
inline std::wstring WindowsPath(const std::wstring& p) {
    if(p.rfind(L"\\\\?\\",0)==0) return p;
    if(p.rfind(L"\\\\",0)==0) return L"\\\\?\\UNC\\"+p.substr(2);
    if(p.size()>2 && p[1]==L':') return L"\\\\?\\"+p;
    return p;
}
inline bool EnsureDirectory(const std::wstring& path) {
    if(path.empty()) return false;
    const auto native_path=WindowsPath(path);
    DWORD attributes=GetFileAttributesW(native_path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES) {
        const auto slash=path.find_last_of(L"\\/");
        if(slash!=std::wstring::npos && slash>2 && !EnsureDirectory(path.substr(0,slash))) return false;
        if(CreateDirectoryW(native_path.c_str(),nullptr)) return true;
        if(GetLastError()!=ERROR_ALREADY_EXISTS) return false;
        attributes=GetFileAttributesW(native_path.c_str());
    }
    return attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
}
}
