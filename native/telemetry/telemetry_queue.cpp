#include "../tooling/windows_handle.h"
#include "telemetry_queue.h"
#include "../tooling/windows_last_error.h"
#include "../tooling/windows_known_folder.h"
#include <cwchar>

namespace privacy {
namespace {
using win32::Handle;
struct Search {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~Search(){if(value!=INVALID_HANDLE_VALUE)FindClose(value);}
};
bool Missing(DWORD error){return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND;}
void Failed(QueueCleanupResult& result,DWORD error){++result.failed;result.last_error=error;}
std::wstring Ordinary(std::wstring path) {
    if(path.compare(0,8,L"\\\\?\\UNC\\")==0)return L"\\\\"+path.substr(8);
    if(path.compare(0,4,L"\\\\?\\")==0)return path.substr(4);
    return path;
}
bool ExactPath(HANDLE handle,const std::wstring& path) {
    std::wstring actual(32768,L'\0'),expected(32768,L'\0');
    const DWORD got=GetFinalPathNameByHandleW(handle,actual.data(),static_cast<DWORD>(actual.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    const DWORD full=GetFullPathNameW(path.c_str(),static_cast<DWORD>(expected.size()),expected.data(),nullptr);
    if(!got||got>=actual.size()||!full||full>=expected.size())return false;
    actual.resize(got);expected.resize(full);
    return !_wcsicmp(Ordinary(actual).c_str(),Ordinary(expected).c_str());
}
bool Directory(HANDLE handle,const std::wstring& path,QueueCleanupResult& result) {
    FILE_ATTRIBUTE_TAG_INFO info{};
    if(!GetFileInformationByHandleEx(handle,FileAttributeTagInfo,&info,sizeof(info))){Failed(result,GetLastError());return false;}
    if(!(info.FileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(info.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||!ExactPath(handle,path)){
        ++result.skipped;return false;
    }
    return true;
}
HANDLE OpenDirectory(const std::wstring& path) {
    // No delete sharing: hold the validated directory in place until cleanup ends.
    return CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
}
void RemoveQueue(const std::wstring& account,QueueCleanupResult& result) {
    Handle directory(OpenDirectory(account));
    if(directory.value==INVALID_HANDLE_VALUE){const auto error=GetLastError();if(!Missing(error))Failed(result,error);return;}
    if(!Directory(directory.value,account,result))return;
    const auto path=account+L"\\pending-tracking.pb";
    Handle file(CreateFileW(path.c_str(),DELETE|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
    if(file.value==INVALID_HANDLE_VALUE){const auto error=GetLastError();if(!Missing(error))Failed(result,error);return;}
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file.value,&info)){Failed(result,GetLastError());return;}
    if((info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||!ExactPath(file.value,path)){
        ++result.skipped;return;
    }
    // Delete the validated handle, never reopen an unchecked pathname.
    FILE_DISPOSITION_INFO disposition{TRUE};
    if(!SetFileInformationByHandle(file.value,FileDispositionInfo,&disposition,sizeof(disposition))){Failed(result,GetLastError());return;}
    ++result.removed;result.bytes+=(std::uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
}
}
QueueCleanupResult ClearTrackingQueues(const std::wstring& users_root,bool enabled) noexcept {
    win32::LastError previous;QueueCleanupResult result;
    if(!enabled)return result;
    try {
        Handle directory(OpenDirectory(users_root));
        if(directory.value==INVALID_HANDLE_VALUE){const auto error=GetLastError();if(!Missing(error))Failed(result,error);return result;}
        if(!Directory(directory.value,users_root,result))return result;
        WIN32_FIND_DATAW entry{};Search search;
        search.value=FindFirstFileW((users_root+L"\\*-user").c_str(),&entry);
        if(search.value==INVALID_HANDLE_VALUE){const auto error=GetLastError();if(!Missing(error))Failed(result,error);return result;}
        do {
            if(!(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))continue;
            const std::wstring name=entry.cFileName;
            // Recheck exact suffix rather than relying on Windows wildcard/8.3 matching.
            if(name.size()<=5||_wcsicmp(name.c_str()+name.size()-5,L"-user"))continue;
            if(entry.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT){++result.skipped;continue;}
            RemoveQueue(users_root+L"\\"+name,result);
        }while(FindNextFileW(search.value,&entry));
        const auto error=GetLastError();if(error!=ERROR_NO_MORE_FILES)Failed(result,error);
    }catch(...){Failed(result,ERROR_NOT_ENOUGH_MEMORY);}
    return result;
}
QueueCleanupResult ClearRoamingTrackingQueues(bool enabled) noexcept {
    win32::LastError previous;QueueCleanupResult result;
    if(!enabled)return result;
    try {
        std::wstring roaming;
        const auto status=history::ReadKnownFolder(FOLDERID_RoamingAppData,roaming);
        if(FAILED(status))Failed(result,HRESULT_CODE(status));
        else result=ClearTrackingQueues(roaming+L"\\Spotify\\Users",true);
    }catch(...){Failed(result,ERROR_NOT_ENOUGH_MEMORY);}
    return result;
}
}
