#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "marked_recording.h"
#include "history_settings.h"
#include "metadata_update_windows.h"

namespace history {
namespace {
struct Handle {
    HANDLE value;
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
}
bool ReadMarkedRecording(const std::wstring& path,MarkedRecording& out,std::string& error) {
    out={};
    const Handle file{CreateFileW(WindowsPath(path).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,
                                  nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    BY_HANDLE_FILE_INFORMATION info{};
    if(file.value==INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file.value,&info) ||
       (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
        error="cannot open recording metadata";
        return false;
    }
    const auto size=uint64_t(info.nFileSizeHigh)<<32|info.nFileSizeLow;
    if(!ReadMarkedRecording(size,[&](uint64_t at,void* dest,size_t bytes){
        LARGE_INTEGER pos{};
        pos.QuadPart=static_cast<LONGLONG>(at);
        DWORD read=0;
        return SetFilePointerEx(file.value,pos,nullptr,FILE_BEGIN) &&
               ReadFile(file.value,dest,DWORD(bytes),&read,nullptr) && read==bytes;
    },out,error))return false;
    out.write_time=uint64_t(info.ftLastWriteTime.dwHighDateTime)<<32|info.ftLastWriteTime.dwLowDateTime;
    return true;
}
bool BackfillMarkedDate(const std::wstring& path,unsigned expected_generation) {
    const auto settings=GetSettings();
    if(settings.generation!=expected_generation || !settings.downloads ||
       !settings.metadata || !settings.update_existing_metadata || !settings.marked_when_downloaded)return false;
    MarkedRecording marked;
    std::string error;
    if(!ReadMarkedRecording(path,marked,error) || !marked.complete || !marked.owned ||
       marked.uri.empty() || !marked.saved_utc.empty())return false;
    Catalog catalog;
    catalog.spotify_uri=marked.uri;
    return UpdateRecordingMetadata(path,{},catalog,expected_generation)==FileMetadataUpdate::Updated;
}
}
