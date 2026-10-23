#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "file_publication.h"
#include "history_settings.h"
#include <cstring>
#include <vector>
namespace history {
#ifdef FLOGGFY_PUBLICATION_TEST
void PublicationTestPhase(const wchar_t* phase,const std::wstring& path);
#endif
namespace {
void TestPhase(const wchar_t* phase,const std::wstring& path) {
#ifdef FLOGGFY_PUBLICATION_TEST
    PublicationTestPhase(phase,path);
#else
    (void)phase; (void)path;
#endif
}
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~Handle() { if(value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
};
std::wstring UniqueSibling(const std::wstring& path,const wchar_t* label) {
    static volatile LONG sequence=0;
    return path+label+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(InterlockedIncrement(&sequence));
}
bool WriteNew(const std::wstring& path,const void* bytes,size_t size) {
    if(!bytes || !size || size>MAXDWORD) return false;
    auto name=WindowsPath(path);
    bool ok=false;
    {
        Handle file{CreateFileW(name.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)};
        if(file.value==INVALID_HANDLE_VALUE) return false;
        DWORD written=0;
        ok=WriteFile(file.value,bytes,DWORD(size),&written,nullptr) && written==size && FlushFileBuffers(file.value);
    }
    if(!ok) DeleteFileW(name.c_str());
    return ok;
}
bool RenameHandle(HANDLE file,const std::wstring& destination) {
    auto path=WindowsPath(destination);
    // FILE_RENAME_INFO uses an absolute path. ReplaceIfExists remains false.
    size_t bytes=offsetof(FILE_RENAME_INFO,FileName)+(path.size()+1)*sizeof(wchar_t);
    std::vector<uint8_t> buffer(bytes,0);
    auto info=reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    info->FileNameLength=DWORD(path.size()*sizeof(wchar_t));
    std::memcpy(info->FileName,path.data(),info->FileNameLength);
    return SetFileInformationByHandle(file,FileRenameInfo,info,DWORD(bytes))!=0;
}
bool SameFile(HANDLE handle,const std::wstring& path) {
    Handle current{CreateFileW(WindowsPath(path).c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    BY_HANDLE_FILE_INFORMATION a={},b={};
    return current.value!=INVALID_HANDLE_VALUE && GetFileInformationByHandle(handle,&a) &&
        GetFileInformationByHandle(current.value,&b) && a.dwVolumeSerialNumber==b.dwVolumeSerialNumber &&
        a.nFileIndexHigh==b.nFileIndexHigh && a.nFileIndexLow==b.nFileIndexLow;
}
bool PrepareBackupSlot(const std::wstring& path,const Catalog& catalog) {
    const auto previous=path+L".previous";
    Handle backup{CreateFileW(WindowsPath(previous).c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    if(backup.value==INVALID_HANDLE_VALUE) {
        const auto error=GetLastError();
        return error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND;
    }
    // The caller holds the current target against writes/deletion. Lock the
    // backup too, then inspect both exact objects before retiring any bytes.
    RecordingFile current,retained;
    if(!SameFile(backup.value,previous) || !ReadRecording(path,current) ||
       (!current.complete && !current.owned) || !MatchesRecording(current,catalog) ||
       !ReadRecording(previous,retained) || !retained.owned || !MatchesRecording(retained,catalog)) return false;
    const auto& a=current.quality;const auto& b=retained.quality;
    const bool equal=a.codec==b.codec && a.rate==b.rate && a.channels==b.channels &&
                     a.bits==b.bits && a.bitrate==b.bitrate;
    if(current.complete && (!retained.complete || equal || HigherQuality(a,b))) {
        FILE_DISPOSITION_INFO disposition={TRUE};
        return SetFileInformationByHandle(backup.value,FileDispositionInfo,&disposition,sizeof(disposition))!=0;
    }
    // Keep a better or incomparable old recording as an ordinary audio file.
    // Bounded no-overwrite renames cannot destroy an occupied recovery name.
    const auto dot=path.find_last_of(L'.');
    const auto stem=dot==std::wstring::npos?path:path.substr(0,dot);
    const auto extension=dot==std::wstring::npos?std::wstring():path.substr(dot);
    for(unsigned attempt=0;attempt<32;++attempt) {
        const auto target=stem+L" [recovered"+(attempt?L"-"+std::to_wstring(attempt):L"")+L"]"+extension;
        if(RenameHandle(backup.value,target))return true;
        const auto error=GetLastError();
        if(error!=ERROR_ALREADY_EXISTS && error!=ERROR_FILE_EXISTS)return false;
    }
    return false;
}
Catalog CatalogFromTags(const Tags& tags) {
    auto field=[&](const char* name) { for(const auto& pair:tags.fields) if(pair.first==name) return pair.second; return std::string(); };
    auto wide=[](const std::string& value) {
        int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);
        std::wstring result(size_t(n),L'\0');
        if(n) MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),result.data(),n);
        return result;
    };
    Catalog catalog;
    catalog.title=wide(field("TITLE")); catalog.album=wide(field("ALBUM"));
    auto artist=field("ALBUMARTIST"); if(artist.empty()) artist=field("ARTIST");
    catalog.artist=wide(artist); catalog.spotify_uri=field("SPOTIFY_URI");
    catalog.track=unsigned(std::strtoul(field("TRACKNUMBER").c_str(),nullptr,10));
    catalog.disc=unsigned(std::strtoul(field("DISCNUMBER").c_str(),nullptr,10));
    catalog.duration=std::strtod(field("HISTORY_MEDIA_DURATION").c_str(),nullptr);
    return catalog;
}
}
FilePublication PublishBytes(const std::wstring& path,const Quality& quality,const void* bytes,size_t size,const Catalog& catalog) {
    RecordingFile incoming;
    if(path.empty() || !InspectRecording(static_cast<const uint8_t*>(bytes),size,incoming) ||
       !incoming.complete || !MatchesRecording(incoming,catalog)) return FilePublication::Failed;
    if(incoming.quality.codec!=quality.codec || incoming.quality.rate!=quality.rate ||
       incoming.quality.channels!=quality.channels || incoming.quality.bits!=quality.bits || incoming.quality.bitrate!=quality.bitrate) return FilePublication::Failed;
    RecoverRecording(path,catalog);
    auto decision=DecideSave(path,quality,catalog);
    if(decision==SaveDecision::Skip) return FilePublication::Skipped;
    auto staging=UniqueSibling(path,L".complete-");
    if(!WriteNew(staging,bytes,size)) return FilePublication::Failed;
    TestPhase(L"staged",path);
    if(decision==SaveDecision::NewFile) {
        // No replace flag: a destination created during staging belongs to the
        // other writer, even when its quality happens to be lower.
        bool ok=MoveFileExW(WindowsPath(staging).c_str(),WindowsPath(path).c_str(),MOVEFILE_WRITE_THROUGH)!=0;
        if(!ok) DeleteFileW(WindowsPath(staging).c_str());
        return ok?FilePublication::Saved:FilePublication::Failed;
    }
    // Lock the exact old object against writes/deletes, recheck its identity and
    // move that object aside through its handle. Promotion never overwrites a
    // raced pathname. A crash leaves the old recording in a deterministic .previous sibling, recovered by the next resolver/publication.
    Handle old{CreateFileW(WindowsPath(path).c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    bool upgrade=old.value!=INVALID_HANDLE_VALUE && SameFile(old.value,path) &&
        DecideSave(path,quality,catalog)==SaveDecision::Upgrade;
    auto backup=path+L".previous";
    bool moved=upgrade && PrepareBackupSlot(path,catalog) && RenameHandle(old.value,backup);
    if(moved) TestPhase(L"reserved",path);
    bool published=moved && MoveFileExW(WindowsPath(staging).c_str(),WindowsPath(path).c_str(),MOVEFILE_WRITE_THROUGH);
    if(published) {
        FILE_DISPOSITION_INFO disposition={TRUE};
        SetFileInformationByHandle(old.value,FileDispositionInfo,&disposition,sizeof(disposition));
    } else {
        if(moved) RenameHandle(old.value,path); // no replacement; preserve any raced file
        DeleteFileW(WindowsPath(staging).c_str());
    }
    return published?FilePublication::Upgraded:FilePublication::Failed;
}
FilePublication PublishBytes(const std::wstring& path,const Quality& quality,const void* bytes,size_t size) {
    RecordingFile incoming;
    if(!InspectRecording(static_cast<const uint8_t*>(bytes),size,incoming)) return FilePublication::Failed;
    return PublishBytes(path,quality,bytes,size,CatalogFromTags(incoming.tags));
}
}
