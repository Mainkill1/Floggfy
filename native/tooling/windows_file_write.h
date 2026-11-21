#pragma once
#include "windows_path.h"
#include <cstdint>
#include <cstring>
#include <vector>
#include <cstddef>

namespace history {
inline bool WriteAllBytes(HANDLE file,const void* bytes,std::size_t size) {
    auto data=static_cast<const char*>(bytes);
    while(size) {
        const DWORD chunk=size>MAXDWORD?MAXDWORD:static_cast<DWORD>(size);
        DWORD written=0;
        if(!WriteFile(file,data,chunk,&written,nullptr)||!written)return false;
        data+=written;
        size-=written;
    }
    return true;
}

// Rename the object verified by the caller. An occupied destination must survive.
inline bool RenameWithoutReplacing(HANDLE file, const std::wstring& destination) {
    const auto path = WindowsPath(destination);
    const auto bytes = offsetof(FILE_RENAME_INFO, FileName) + (path.size() + 1) * sizeof(wchar_t);
    std::vector<std::uint8_t> buffer(bytes, 0);
    auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    info->FileNameLength = static_cast<DWORD>(path.size() * sizeof(wchar_t));
    std::memcpy(info->FileName, path.data(), info->FileNameLength);
    return SetFileInformationByHandle(file, FileRenameInfo, info, static_cast<DWORD>(bytes)) != 0;
}
}

namespace win32 {
inline bool DeleteOnClose(HANDLE file) noexcept {
    FILE_DISPOSITION_INFO disposition{TRUE};
    return SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition)) != 0;
}
}
