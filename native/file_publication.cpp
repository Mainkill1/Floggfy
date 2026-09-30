#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "file_publication.h"
#include "history_settings.h"
namespace history {
static bool WriteNew(const std::wstring& path,const void* bytes,size_t size) {
    if(!size || size>MAXDWORD) return false;
    auto name=WindowsPath(path);
    HANDLE file=CreateFileW(name.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    DWORD written=0;
    bool ok=WriteFile(file,bytes,DWORD(size),&written,nullptr) && written==size;
    CloseHandle(file);
    if(!ok) DeleteFileW(name.c_str());
    return ok;
}
FilePublication PublishBytes(const std::wstring& path,const Quality& quality,const void* bytes,size_t size) {
    auto decision=DecideSave(path,quality);
    if(decision==SaveDecision::Skip) return FilePublication::Skipped;
    if(decision==SaveDecision::NewFile) return WriteNew(path,bytes,size) ? FilePublication::Saved : FilePublication::Failed;
    // Stage the completed file only; never write track fragments to disk.
    static volatile LONG sequence=0;
    auto staging=path+L".complete-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(InterlockedIncrement(&sequence));
    if(!WriteNew(staging,bytes,size)) return FilePublication::Failed;
    bool ok=DecideSave(path,quality)==SaveDecision::Upgrade &&
        MoveFileExW(WindowsPath(staging).c_str(),WindowsPath(path).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    if(!ok) DeleteFileW(WindowsPath(staging).c_str());
    return ok ? FilePublication::Upgraded : FilePublication::Failed;
}
}
