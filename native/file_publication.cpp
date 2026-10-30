#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "file_publication.h"
#include "history_settings.h"
#include "metadata_update_core.h"
#include <algorithm>
#include <cstring>
#include <vector>
#include <cwchar>
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
struct StagingFile {
    std::wstring windows_path;
    ~StagingFile() { DeleteFileW(windows_path.c_str()); }
};
bool ReadLockedRecording(HANDLE file,std::vector<uint8_t>& bytes) {
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file,&size) || size.QuadPart<4 || size.QuadPart>512LL*1024*1024)return false;
    bytes.resize(size_t(size.QuadPart));
    for(size_t offset=0;offset<bytes.size();) {
        const DWORD wanted=DWORD(std::min<size_t>(1024*1024,bytes.size()-offset));
        DWORD read=0;
        if(!ReadFile(file,bytes.data()+offset,wanted,&read,nullptr) || read!=wanted)return false;
        offset+=read;
    }
    return true;
}
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
bool PermitCurrent(bool(*is_current)(void*),void* context) noexcept {
    try{return !is_current||is_current(context);}catch(...){return false;}
}
FilePublication PromoteReplacement(const std::wstring& path,const std::wstring& staging,const Catalog& catalog,HANDLE old,
    bool(*is_current)(void*)=nullptr,void* context=nullptr) {
    auto backup=path+L".previous";
    bool moved=PrepareBackupSlot(path,catalog) && RenameHandle(old,backup);
    if(moved) TestPhase(L"reserved",path);
    const bool permitted=PermitCurrent(is_current,context);
    bool published=moved && permitted && MoveFileExW(WindowsPath(staging).c_str(),WindowsPath(path).c_str(),MOVEFILE_WRITE_THROUGH);
    if(published) {
        FILE_DISPOSITION_INFO disposition={TRUE};
        SetFileInformationByHandle(old,FileDispositionInfo,&disposition,sizeof(disposition));
    } else {
        if(moved) RenameHandle(old,path); // no replacement; preserve any raced file
        DeleteFileW(WindowsPath(staging).c_str());
    }
    return published?FilePublication::Upgraded:!permitted?FilePublication::Skipped:FilePublication::Failed;
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
FilenameConversion ConvertRecordingFilename(const std::wstring& source,const std::wstring& title_path,
    const Catalog& catalog,std::wstring& renamed_path,FilenameRenamePermit is_current,void* context) {
    try {
        auto same=[](const std::wstring& a,const std::wstring& b) {
            return CompareStringOrdinal(a.c_str(),int(a.size()),b.c_str(),int(b.size()),TRUE)==CSTR_EQUAL;
        };
        const auto plain=RecordingPathCandidates(title_path,catalog,false);
        if(std::any_of(plain.begin(),plain.end(),[&](const auto& path){return same(path,source);}))
            return FilenameConversion::Unchanged;
        const auto known=RecordingPathCandidates(title_path,catalog,true);
        if(std::none_of(known.begin(),known.end(),[&](const auto& path){return same(path,source);}))
            return FilenameConversion::Unchanged;
        const auto slash=source.find_last_of(L"\\/");
        if(slash==std::wstring::npos)return FilenameConversion::Unchanged;
        const auto filename=source.substr(slash+1);
        std::wstring track;
        if(catalog.track){wchar_t prefix[20];std::swprintf(prefix,20,L"%02u - ",catalog.track);track=prefix;}
        std::vector<std::wstring> prefixes;
        if(catalog.disc>1)prefixes.push_back(L"Disc "+std::to_wstring(catalog.disc)+L" - "+track);
        if(!track.empty())prefixes.push_back(track);
        std::wstring target;
        for(const auto& prefix:prefixes)if(filename.rfind(prefix,0)==0) {
            target=source.substr(0,slash+1)+filename.substr(prefix.size());break;
        }
        if(target.empty() || std::none_of(plain.begin(),plain.end(),[&](const auto& path){return same(path,target);}))
            return FilenameConversion::Unchanged;

        // Freeze the exact source against writes/deletion before validating
        // ownership and identity. The existing rename helper never overwrites.
        if(is_current && !is_current(context))return FilenameConversion::Unchanged;
        Handle file{CreateFileW(WindowsPath(source).c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
        RecordingFile existing;
        if(file.value==INVALID_HANDLE_VALUE || !SameFile(file.value,source) ||
           !ReadRecording(source,existing) || !existing.complete || !existing.owned ||
           !MatchesRecording(existing,catalog))return FilenameConversion::Failed;
        // Allocate the caller's result before committing the
        // pathname change, so allocation failure cannot hide a successful move.
        std::wstring result=target;
        // Inspection may have waited on slow storage. Recheck cancellation and
        // settings at commit, after allocation and before the atomic rename.
        if(is_current && !is_current(context))return FilenameConversion::Unchanged;
        if(!RenameHandle(file.value,target)) {
            const auto error=GetLastError();
            return error==ERROR_ALREADY_EXISTS || error==ERROR_FILE_EXISTS
                ?FilenameConversion::Unchanged:FilenameConversion::Failed;
        }
        renamed_path.swap(result);return FilenameConversion::Renamed;
    }catch(...){return FilenameConversion::Failed;}
}
FilePublication PublishBytes(const std::wstring& path,const Quality& quality,const void* bytes,size_t size,const Catalog& catalog,
                             size_t* published_bytes) {
    if(published_bytes)*published_bytes=0;
    RecordingFile incoming;
    if(path.empty() || !InspectRecording(static_cast<const uint8_t*>(bytes),size,incoming) ||
       !incoming.complete || !MatchesRecording(incoming,catalog)) return FilePublication::Failed;
    if(incoming.quality.codec!=quality.codec || incoming.quality.rate!=quality.rate ||
       incoming.quality.channels!=quality.channels || incoming.quality.bits!=quality.bits || incoming.quality.bitrate!=quality.bitrate) return FilePublication::Failed;
    RecoverRecording(path,catalog);
    auto decision=DecideSave(path,quality,catalog);
    if(decision==SaveDecision::Skip) return FilePublication::Skipped;
    Handle old;
    std::vector<uint8_t> preserved;
    if(decision==SaveDecision::Upgrade) {
        // Freeze the exact existing object BEFORE composing replacement tags.
        // The same handle protects this snapshot through reservation/promotion.
        old.value=CreateFileW(WindowsPath(path).c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        RecordingFile current;
        if(old.value==INVALID_HANDLE_VALUE || !SameFile(old.value,path) ||
           DecideSave(path,quality,catalog,&current)!=SaveDecision::Upgrade)return FilePublication::Failed;
        if(current.complete) {
            std::vector<uint8_t> existing;
            if(!ReadLockedRecording(old.value,existing))return FilePublication::Failed;
            const auto first=static_cast<const uint8_t*>(bytes);
            const std::vector<uint8_t> replacement(first,first+size);
            std::string error;
            if(!PreserveUpgradeMetadata(existing,replacement,catalog,preserved,error))return FilePublication::Failed;
            bytes=preserved.data();size=preserved.size();
        }
        // Proven incomplete owned files are repaired from the new complete
        // capture; their unverified metadata cannot authorize preservation.
    }
    auto staging=UniqueSibling(path,L".complete-");
    StagingFile cleanup{WindowsPath(staging)};
    if(!WriteNew(staging,bytes,size)) return FilePublication::Failed;
    TestPhase(L"staged",path);
    if(decision==SaveDecision::NewFile) {
        // No replace flag: a destination created during staging belongs to the
        // other writer, even when its quality happens to be lower.
        bool ok=MoveFileExW(WindowsPath(staging).c_str(),WindowsPath(path).c_str(),MOVEFILE_WRITE_THROUGH)!=0;
        if(ok && published_bytes)*published_bytes=size;
        return ok?FilePublication::Saved:FilePublication::Failed;
    }
    // Promotion never overwrites a raced pathname. An interruption retains
    // the old object in .previous for the next resolver/publication to recover.
    const auto result=PromoteReplacement(path,staging,catalog,old.value);
    if(result==FilePublication::Upgraded && published_bytes)*published_bytes=size;
    return result;
}
FilePublication PublishMetadataBytes(const std::wstring& path,const void* bytes,size_t size,const Catalog& catalog,void* locked_file,
    bool(*is_current)(void*),void* context) {
    RecordingFile incoming,current;
    if(!locked_file || locked_file==INVALID_HANDLE_VALUE || !SameFile(locked_file,path) ||
       !ReadRecording(path,current) || !current.complete || !current.owned || !MatchesRecording(current,catalog) ||
       !InspectRecording(static_cast<const uint8_t*>(bytes),size,incoming) || !incoming.complete || !incoming.owned ||
       !MatchesRecording(incoming,catalog)) return FilePublication::Failed;
    const auto& a=current.quality;const auto& b=incoming.quality;
    if(a.codec!=b.codec || a.rate!=b.rate || a.channels!=b.channels || a.bits!=b.bits || a.bitrate!=b.bitrate)
        return FilePublication::Failed;
    const auto staging=UniqueSibling(path,L".complete-lyrics-");
    StagingFile cleanup{WindowsPath(staging)};
    if(!WriteNew(staging,bytes,size))return FilePublication::Failed;
    TestPhase(L"staged",path);
    if(!PermitCurrent(is_current,context))return FilePublication::Skipped;
    return PromoteReplacement(path,staging,catalog,locked_file,is_current,context);
}
FilePublication PublishBytes(const std::wstring& path,const Quality& quality,const void* bytes,size_t size) {
    RecordingFile incoming;
    if(!InspectRecording(static_cast<const uint8_t*>(bytes),size,incoming)) return FilePublication::Failed;
    return PublishBytes(path,quality,bytes,size,CatalogFromTags(incoming.tags));
}
}
