#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "file_publication.h"
#include "history_settings.h"
#include "metadata_update_core.h"
#include <algorithm>
#include <cstring>
#include <vector>
#include <cwchar>
#include <new>
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
    if(!GetFileSizeEx(file,&size) || size.QuadPart<4 || size.QuadPart>static_cast<LONGLONG>(kMaximumRecordingBytes))return false;
    if(RecordingReadMemoryScope::HasPermit()) {
        RecordingMemoryPlan memory;
        if(!PlanLockedRecordingMemory(file,memory)||!RecordingReadMemoryScope::AdmitCurrent(memory.inspection_peak))return false;
    }
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
    RecordingReadMemoryScope memory;RecordingFile current,retained;
    if(!SameFile(backup.value,previous) || !ReadRecording(path,current) ||
       (!current.complete && !current.owned) || !MatchesRecording(current,catalog))return false;
    memory.SetRetainedBytes(RecordingMetadataMemoryUsage(current));
    if(!ReadRecording(previous,retained) || !retained.owned || !MatchesRecording(retained,catalog))return false;
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
FilePublication PromoteReplacement(const std::wstring& path,const std::wstring& staging,const Catalog& catalog,HANDLE old) {
    auto backup=path+L".previous";
    bool moved=PrepareBackupSlot(path,catalog) && RenameHandle(old,backup);
    if(moved) TestPhase(L"reserved",path);
    bool published=moved && MoveFileExW(WindowsPath(staging).c_str(),WindowsPath(path).c_str(),MOVEFILE_WRITE_THROUGH);
    if(published) {
        FILE_DISPOSITION_INFO disposition={TRUE};
        SetFileInformationByHandle(old,FileDispositionInfo,&disposition,sizeof(disposition));
    } else {
        if(moved) RenameHandle(old,path); // no replacement; preserve any raced file
        DeleteFileW(WindowsPath(staging).c_str());
    }
    return published?FilePublication::Upgraded:FilePublication::Failed;
}
}
bool CommitArchiveStage(void* staged_file,const std::wstring& destination) {
    return staged_file&&staged_file!=INVALID_HANDLE_VALUE&&RenameHandle(static_cast<HANDLE>(staged_file),destination);
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
                             size_t* published_bytes,PublicationPermit permit,void* context,PublicationMemoryPermit memory_permit,void* memory_context) {
    RecordingReadMemoryScope memory(memory_permit,memory_context);
    try {
    if(published_bytes)*published_bytes=0;
    RecordingMemoryPlan incoming_memory;
    if(RecordingReadMemoryScope::HasPermit()) {
        if(!ProbeRecordingMemory(static_cast<const uint8_t*>(bytes),size,incoming_memory))return FilePublication::Failed;
        if(!memory.Request(incoming_memory.inspection_peak-size))return FilePublication::ResourceWait;
    }
    RecordingFile incoming;
    if(path.empty() || !InspectRecording(static_cast<const uint8_t*>(bytes),size,incoming) ||
       !incoming.complete || !MatchesRecording(incoming,catalog)) return FilePublication::Failed;
    if(incoming.quality.codec!=quality.codec || incoming.quality.rate!=quality.rate ||
       incoming.quality.channels!=quality.channels || incoming.quality.bits!=quality.bits || incoming.quality.bitrate!=quality.bitrate) return FilePublication::Failed;
    memory.SetRetainedBytes(RecordingMetadataMemoryUsage(incoming));
    RecoverRecording(path,catalog);
    if(memory.Denied())return FilePublication::ResourceWait;
    auto decision=DecideSave(path,quality,catalog);
    if(memory.Denied())return FilePublication::ResourceWait;
    if(decision==SaveDecision::Skip) return permit&&!permit(context)?FilePublication::Replan:FilePublication::Skipped;
    Handle old;
    std::vector<uint8_t> preserved;
    if(decision==SaveDecision::Upgrade) {
        // Freeze the exact existing object BEFORE composing replacement tags.
        // The same handle protects this snapshot through reservation/promotion.
        old.value=CreateFileW(WindowsPath(path).c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        RecordingFile current;
        if(old.value==INVALID_HANDLE_VALUE || !SameFile(old.value,path) ||
           DecideSave(path,quality,catalog,&current)!=SaveDecision::Upgrade)
            return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
        if(current.complete) {
            if(RecordingReadMemoryScope::HasPermit()) {
                RecordingMemoryPlan old_memory;size_t peak=0;
                if(!PlanLockedRecordingMemory(old.value,old_memory)||
                   !EstimatePublicationUpgradeMemory(quality.codec,old_memory.file_bytes,size,
                       old_memory.metadata_bytes,incoming_memory.metadata_bytes,peak))return FilePublication::ResourceWait;
                if(!memory.Request(peak))return FilePublication::ResourceWait;
            }
            std::vector<uint8_t> existing;
            if(!ReadLockedRecording(old.value,existing))return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
            const auto first=static_cast<const uint8_t*>(bytes);
            const std::vector<uint8_t> replacement(first,first+size);
            std::string error;
            if(!PreserveUpgradeMetadata(existing,replacement,catalog,preserved,error))return FilePublication::Failed;
            bytes=preserved.data();size=preserved.size();
        }
        // Proven incomplete owned files are repaired from the new complete
        // capture; their unverified metadata cannot authorize preservation.
    }
    memory.SetRetainedBytes(RecordingMetadataMemoryUsage(incoming)+preserved.capacity());
    auto staging=UniqueSibling(path,L".complete-");
    StagingFile cleanup{WindowsPath(staging)};
    if(!WriteNew(staging,bytes,size)) return FilePublication::Failed;
    TestPhase(L"staged",path);
    if(permit&&!permit(context))return FilePublication::Replan;
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
    if(memory.Denied())return FilePublication::ResourceWait;
    if(result==FilePublication::Upgraded && published_bytes)*published_bytes=size;
    return result;
    }catch(const std::bad_alloc&){return FilePublication::ResourceWait;}
    catch(...){return FilePublication::Failed;}
}

namespace {
uint64_t FileNumber(const BY_HANDLE_FILE_INFORMATION& info) {
    return (uint64_t(info.nFileIndexHigh)<<32)|info.nFileIndexLow;
}
uint64_t RecordingHash(const uint8_t* bytes,size_t size,uint64_t hash=14695981039346656037ull) {
    for(size_t i=0;i<size;++i){hash^=bytes[i];hash*=1099511628211ull;}return hash;
}
bool ReceiptSourceUnchanged(HANDLE file,const FormatUpgradeReceipt& receipt,bool& unchanged) {
    unchanged=false;LARGE_INTEGER size{},zero{};
    if(!GetFileSizeEx(file,&size)||size.QuadPart<0)return false;
    if(uint64_t(size.QuadPart)!=receipt.source_bytes)return true;
    if(!SetFilePointerEx(file,zero,nullptr,FILE_BEGIN))return false;
    uint8_t bytes[16384];uint64_t hash=14695981039346656037ull;
    for(size_t at=0;at<receipt.source_bytes;) {
        const auto count=DWORD(std::min(sizeof(bytes),receipt.source_bytes-at));DWORD read=0;
        if(!ReadFile(file,bytes,count,&read,nullptr)||read!=count)return false;
        hash=RecordingHash(bytes,count,hash);at+=count;
    }
    if(!SetFilePointerEx(file,zero,nullptr,FILE_BEGIN))return false;
    unchanged=hash==receipt.source_hash;return true;
}
bool ReceiptQuality(const RecordingFile& recording,const FormatUpgradeReceipt& receipt) {
    const auto& a=recording.quality;const auto& b=receipt.quality;
    const bool equal=a.codec==b.codec&&a.rate==b.rate&&a.channels==b.channels&&a.bits==b.bits&&a.bitrate==b.bitrate;
    return recording.complete&&recording.owned&&MatchesRecording(recording,receipt.catalog)&&
        HigherQuality(a,receipt.source_quality)&&(equal||HigherQuality(a,b));
}
FilePublication CompleteFormatUpgradeHeld(FormatUpgradeReceipt& receipt,HANDLE old,size_t* published_bytes) {
    RecordingReadMemoryScope memory;
    TestPhase(L"format-final-read",receipt.destination);
    Handle final{CreateFileW(WindowsPath(receipt.destination).c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    std::vector<uint8_t> final_bytes;RecordingFile recording;
    if(final.value==INVALID_HANDLE_VALUE||!SameFile(final.value,receipt.destination)||
       !ReadLockedRecording(final.value,final_bytes)||
       !InspectRecording(final_bytes.data(),final_bytes.size(),recording)||!ReceiptQuality(recording,receipt))return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
    RecordingMemoryPlan final_memory;
    if(RecordingReadMemoryScope::HasPermit()) {
        if(!ProbeRecordingMemory(final_bytes.data(),final_bytes.size(),final_memory))return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
        memory.SetRetainedBytes(final_bytes.capacity()+RecordingMetadataMemoryUsage(recording));
    }
    if(!receipt.metadata_preserved&&old!=INVALID_HANDLE_VALUE) {
        if(RecordingReadMemoryScope::HasPermit()) {
            RecordingMemoryPlan old_memory;size_t peak=0;
            if(!PlanLockedRecordingMemory(old,old_memory)||
               !EstimatePublicationUpgradeMemory(receipt.quality.codec,old_memory.file_bytes,final_bytes.size(),
                   old_memory.metadata_bytes,final_memory.metadata_bytes,peak))return FilePublication::ResourceWait;
            memory.SetRetainedBytes(0);if(!memory.Request(peak))return FilePublication::ResourceWait;
            memory.SetRetainedBytes(final_bytes.capacity()+RecordingMetadataMemoryUsage(recording));
        }

        std::vector<uint8_t> old_bytes,merged;std::string error;
        if(!ReadLockedRecording(old,old_bytes)||!PreserveUpgradeMetadata(old_bytes,final_bytes,receipt.catalog,merged,error))return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
        if(RecordingReadMemoryScope::HasPermit())
            memory.SetRetainedBytes(final_bytes.capacity()+old_bytes.capacity()+merged.capacity()+
                RecordingMetadataMemoryUsage(recording));
        if(merged!=final_bytes) {
            if(PublishMetadataBytes(receipt.destination,merged.data(),merged.size(),receipt.catalog,final.value)!=FilePublication::Upgraded)return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
            CloseHandle(final.value);final.value=CreateFileW(WindowsPath(receipt.destination).c_str(),GENERIC_READ,FILE_SHARE_READ,
                nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            final_bytes.clear();
            if(final.value==INVALID_HANDLE_VALUE||!SameFile(final.value,receipt.destination)||
               !ReadLockedRecording(final.value,final_bytes)||
               !InspectRecording(final_bytes.data(),final_bytes.size(),recording)||!ReceiptQuality(recording,receipt))return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
        }
        receipt.metadata_preserved=true;
    }
    if(old!=INVALID_HANDLE_VALUE) {
        TestPhase(L"format-old-delete",receipt.destination);
        FILE_DISPOSITION_INFO disposition={TRUE};
        if(!SetFileInformationByHandle(old,FileDispositionInfo,&disposition,sizeof(disposition)))return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
    }
    if(published_bytes)*published_bytes=final_bytes.size();
    return FilePublication::Upgraded;
}
}
FilePublication CompleteFormatUpgrade(FormatUpgradeReceipt& receipt,size_t* published_bytes,PublicationMemoryPermit memory_permit,void* memory_context) {
    RecordingReadMemoryScope memory(memory_permit,memory_context);
    if(published_bytes)*published_bytes=0;
    if(!receipt.committed)return FilePublication::Failed;
    try {
        Handle old{CreateFileW(WindowsPath(receipt.source).c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
        if(old.value==INVALID_HANDLE_VALUE) {
            const auto error=GetLastError();
            if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)return FilePublication::Failed;
        } else {
            BY_HANDLE_FILE_INFORMATION info{};
            if(!GetFileInformationByHandle(old.value,&info))return FilePublication::Failed;
            // Only the object captured before the original commit is cleanup
            // authority. A later occupant of the old pathname is preserved.
            bool unchanged=false;
            if(info.dwVolumeSerialNumber==receipt.source_volume&&FileNumber(info)==receipt.source_file&&
               !ReceiptSourceUnchanged(old.value,receipt,unchanged))return FilePublication::Failed;
            if(!unchanged) {
                CloseHandle(old.value);old.value=INVALID_HANDLE_VALUE;
            }
        }
        return CompleteFormatUpgradeHeld(receipt,old.value,published_bytes);
    }catch(const std::bad_alloc&){return FilePublication::ResourceWait;}
    catch(...){return FilePublication::Failed;}
}
FilePublication PublishFormatUpgrade(const std::wstring& source,const std::wstring& destination,
    const Quality& quality,const void* bytes,size_t size,const Catalog& catalog,size_t* published_bytes,PublicationPermit permit,void* context,FormatUpgradeReceipt* output_receipt,PublicationMemoryPermit memory_permit,void* memory_context) {
    RecordingReadMemoryScope memory(memory_permit,memory_context);
    if(published_bytes)*published_bytes=0;
    FormatUpgradeReceipt local;auto& receipt=output_receipt?*output_receipt:local;
    try {
        if(source.empty()||destination.empty()||!bytes||!size||
           CompareStringOrdinal(source.c_str(),int(source.size()),destination.c_str(),int(destination.size()),TRUE)==CSTR_EQUAL)
            return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
        RecordingMemoryPlan incoming_memory;
        if(RecordingReadMemoryScope::HasPermit()) {
            if(!ProbeRecordingMemory(static_cast<const uint8_t*>(bytes),size,incoming_memory))return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
            if(!memory.Request(incoming_memory.inspection_peak))return FilePublication::ResourceWait;
        }
        const auto first=static_cast<const uint8_t*>(bytes);std::vector<uint8_t> incoming(first,first+size);RecordingFile next;
        if(!InspectRecording(incoming.data(),incoming.size(),next)||!next.complete||!next.owned||
           !MatchesRecording(next,catalog)||next.quality.codec!=quality.codec||next.quality.rate!=quality.rate||
           next.quality.channels!=quality.channels||next.quality.bits!=quality.bits||next.quality.bitrate!=quality.bitrate)return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
        memory.SetRetainedBytes(incoming.capacity()+RecordingMetadataMemoryUsage(next));
        Handle old{CreateFileW(WindowsPath(source).c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
        std::vector<uint8_t> old_bytes;RecordingFile prior;BY_HANDLE_FILE_INFORMATION old_info{};
        if(old.value==INVALID_HANDLE_VALUE||!SameFile(old.value,source)||!GetFileInformationByHandle(old.value,&old_info)||
           !ReadLockedRecording(old.value,old_bytes)||!InspectRecording(old_bytes.data(),old_bytes.size(),prior)||
           !prior.complete||!prior.owned||prior.quality.codec!=Codec::Vorbis||!MatchesRecording(prior,catalog)||
           !HigherQuality(next.quality,prior.quality))return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
        // Allocate every receipt field before committing. A post-commit failure
        // cannot erase the original destination or send this job to a new root.
        FormatUpgradeReceipt prepared{source,destination,catalog,quality,prior.quality,FileNumber(old_info),old_info.dwVolumeSerialNumber,RecordingHash(old_bytes.data(),old_bytes.size()),old_bytes.size(),false,false,false};
        receipt=std::move(prepared);
        RecordingMemoryPlan old_memory;
        if(RecordingReadMemoryScope::HasPermit()) {
            size_t peak=0;
            if(!ProbeRecordingMemory(old_bytes.data(),old_bytes.size(),old_memory)||
               !EstimatePublicationUpgradeMemory(quality.codec,old_bytes.size(),size,
                   old_memory.metadata_bytes,incoming_memory.metadata_bytes,peak))return FilePublication::ResourceWait;
            memory.SetRetainedBytes(0);if(!memory.Request(peak))return FilePublication::ResourceWait;
        }
        std::vector<uint8_t> preserved;std::string error;
        if(!PreserveUpgradeMetadata(old_bytes,incoming,catalog,preserved,error))return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
        const auto parsed_memory=RecordingMetadataMemoryUsage(next)+RecordingMetadataMemoryUsage(prior);
        memory.SetRetainedBytes(incoming.capacity()+old_bytes.capacity()+preserved.capacity()+parsed_memory);
        size_t saved=0;const auto result=PublishBytes(destination,quality,preserved.data(),preserved.size(),catalog,&saved,permit,context);
        if(result==FilePublication::Failed||result==FilePublication::Replan||result==FilePublication::ResourceWait)return result;
        if(result==FilePublication::Saved||result==FilePublication::Upgraded) {
            receipt.committed=true;receipt.metadata_preserved=true;
        } else {
            // AlreadyPresent is a commit receipt only after verifying that the
            // existing destination satisfies the requested recording and quality.
            Handle current{CreateFileW(WindowsPath(destination).c_str(),GENERIC_READ,FILE_SHARE_READ,
                nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
            std::vector<uint8_t> current_bytes;RecordingFile recording;
            if(current.value==INVALID_HANDLE_VALUE||!SameFile(current.value,destination)||
               !ReadLockedRecording(current.value,current_bytes)||
               !InspectRecording(current_bytes.data(),current_bytes.size(),recording)||!ReceiptQuality(recording,receipt))return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
            receipt.committed=true;
        }
        // Close the large construction snapshots before the verification pass.
        std::vector<uint8_t>().swap(incoming);std::vector<uint8_t>().swap(old_bytes);std::vector<uint8_t>().swap(preserved);
        memory.SetRetainedBytes(parsed_memory);
        LARGE_INTEGER zero{};if(!SetFilePointerEx(old.value,zero,nullptr,FILE_BEGIN))return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
        return CompleteFormatUpgradeHeld(receipt,old.value,published_bytes);
    }catch(const std::bad_alloc&){return FilePublication::ResourceWait;}
    catch(...){return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;}
}
FilePublication PublishMetadataBytes(const std::wstring& path,const void* bytes,size_t size,const Catalog& catalog,void* locked_file) {
    RecordingReadMemoryScope memory;RecordingFile incoming,current;
    if(!locked_file || locked_file==INVALID_HANDLE_VALUE || !SameFile(locked_file,path) ||
       !ReadRecording(path,current) || !current.complete || !current.owned || !MatchesRecording(current,catalog))
        return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
    memory.SetRetainedBytes(RecordingMetadataMemoryUsage(current));
    if(RecordingReadMemoryScope::HasPermit()) {
        RecordingMemoryPlan input;
        if(!ProbeRecordingMemory(static_cast<const uint8_t*>(bytes),size,input)||
           !memory.Request(input.inspection_peak-size))return FilePublication::ResourceWait;
    }
    if(!InspectRecording(static_cast<const uint8_t*>(bytes),size,incoming) || !incoming.complete || !incoming.owned ||
       !MatchesRecording(incoming,catalog)) return FilePublication::Failed;
    const auto& a=current.quality;const auto& b=incoming.quality;
    if(a.codec!=b.codec || a.rate!=b.rate || a.channels!=b.channels || a.bits!=b.bits || a.bitrate!=b.bitrate)
        return FilePublication::Failed;
    memory.SetRetainedBytes(RecordingMetadataMemoryUsage(current)+RecordingMetadataMemoryUsage(incoming));
    const auto staging=UniqueSibling(path,L".complete-lyrics-");
    StagingFile cleanup{WindowsPath(staging)};
    if(!WriteNew(staging,bytes,size))return FilePublication::Failed;
    TestPhase(L"staged",path);
    const auto result=PromoteReplacement(path,staging,catalog,locked_file);
    return memory.Denied()?FilePublication::ResourceWait:result;
}
}
