#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "existing_quality.h"
#include "history_settings.h"
#include <cstring>
#include <algorithm>
#include <utility>
#include <vector>
#include <utility>
namespace history {
namespace {
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~Handle() { if(value!=INVALID_HANDLE_VALUE && value) CloseHandle(value); }
};
bool Missing(const std::wstring& path) {
    if(GetFileAttributesW(WindowsPath(path).c_str())!=INVALID_FILE_ATTRIBUTES) return false;
    DWORD error=GetLastError();
    return error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND;
}
bool Reusable(const std::wstring& path,const Catalog& catalog) {
    RecordingFile existing;
    return ReadRecording(path,existing) && MatchesRecording(existing,catalog) && (existing.complete || existing.owned);
}
bool RestoreBackup(const std::wstring& backup,const std::wstring& destination,const Catalog& catalog) {
    // Bind recovery to the verified object, so another process cannot replace
    // the backup between inspection and rename.
    Handle file{CreateFileW(WindowsPath(backup).c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    RecordingFile old;
    if(file.value==INVALID_HANDLE_VALUE || !ReadRecording(backup,old) || !old.owned || !MatchesRecording(old,catalog)) return false;
    auto path=WindowsPath(destination);
    size_t bytes=offsetof(FILE_RENAME_INFO,FileName)+(path.size()+1)*sizeof(wchar_t);
    std::vector<uint8_t> buffer(bytes,0);
    auto info=reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    info->FileNameLength=DWORD(path.size()*sizeof(wchar_t));
    std::memcpy(info->FileName,path.data(),info->FileNameLength);
    return SetFileInformationByHandle(file.value,FileRenameInfo,info,DWORD(bytes))!=0;
}
}
bool ReadRecording(const std::wstring& path,RecordingFile& out) {
    out={};
    try {
        // Deny writes while inspecting. Checked reads avoid mapped-file page
        // faults escaping C++ exception handling on unavailable storage. The
        // owned buffer is bounded to 512 MiB and is never used on capture paths.
        Handle file{CreateFileW(WindowsPath(path).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
        if(file.value==INVALID_HANDLE_VALUE)return false;
        LARGE_INTEGER size={};
        if(!GetFileSizeEx(file.value,&size)||size.QuadPart<4||size.QuadPart>512LL*1024*1024)return false;
        std::vector<uint8_t> bytes(size_t(size.QuadPart));
        constexpr size_t chunk=1024*1024;
        for(size_t offset=0;offset<bytes.size();) {
            const DWORD wanted=DWORD(std::min(chunk,bytes.size()-offset));DWORD read=0;
            if(!ReadFile(file.value,bytes.data()+offset,wanted,&read,nullptr)||read!=wanted)return false;
            offset+=read;
        }
        LARGE_INTEGER after={};
        if(!GetFileSizeEx(file.value,&after)||after.QuadPart!=size.QuadPart)return false;
        RecordingFile inspected;
        if(!InspectRecording(bytes.data(),bytes.size(),inspected))return false;
        out=std::move(inspected);
        return true;
    }catch(...) {
        // Allocation/parser failures leave no trusted quality or partial tags.
        return false;
    }
}
bool ReadQuality(const std::wstring& path,Quality& out) {
    RecordingFile file;
    if(!ReadRecording(path,file) || !file.complete) return false;
    out=file.quality; return true;
}
bool ExistingRecording(const std::wstring& path,const Catalog& catalog,Quality& out) {
    RecordingFile file;
    if(!ReadRecording(path,file) || !file.complete || !MatchesRecording(file,catalog)) return false;
    out=file.quality; return true;
}
bool RecoverRecording(const std::wstring& path,const Catalog& catalog) {
    if(!Missing(path)) return false;
    return RestoreBackup(path+L".previous",path,catalog);
}
std::wstring ResolveRecordingPath(const std::wstring& base,const Catalog& catalog) {
    if(base.empty()) return {};
    RecoverRecording(base,catalog);
    if(Missing(base) || Reusable(base,catalog)) return base;
    auto slash=base.find_last_of(L"\\/"),dot=base.find_last_of(L'.');
    if(dot==std::wstring::npos || (slash!=std::wstring::npos && dot<slash)) dot=base.size();
    auto stem=base.substr(0,dot)+RecordingSuffix(catalog),extension=base.substr(dot);
    RecordingFile reserved;
    const auto backup=base+L".previous";
    bool restore_reserved=ReadRecording(backup,reserved) && reserved.owned && MatchesRecording(reserved,catalog);
    // Hash collisions and manually occupied suffixes still cannot authorize an
    // overwrite. This is a bounded probe, never a directory/library scan.
    for(unsigned attempt=0;attempt<32;attempt++) {
        auto path=stem+(attempt?L"-"+std::to_wstring(attempt):L"")+extension;
        RecoverRecording(path,catalog);
        if(Missing(path)) {
            if(restore_reserved) {
                if(RestoreBackup(backup,path,catalog)) return path;
                if(!Missing(path) && !Reusable(path,catalog)) continue;
            }
            return path;
        }
        if(Reusable(path,catalog)) return path;
    }
    return {};
}
SaveDecision DecideSave(const std::wstring& path,const Quality&) {
    // Callers without a recording identity may create, but cannot replace.
    return Missing(path)?SaveDecision::NewFile:SaveDecision::Skip;
}
SaveDecision DecideSave(const std::wstring& path,const Quality& incoming,const Catalog& catalog,RecordingFile* inspected) {
    if(inspected)*inspected={};
    if(Missing(path)) return SaveDecision::NewFile;
    RecordingFile existing;
    if(!ReadRecording(path,existing) || !MatchesRecording(existing,catalog)) return SaveDecision::Skip;
    const auto decision=existing.owned && (!existing.complete || HigherQuality(incoming,existing.quality))
        ? SaveDecision::Upgrade : SaveDecision::Skip;
    if(inspected)*inspected=std::move(existing);
    return decision;
}
}
