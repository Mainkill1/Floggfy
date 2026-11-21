#define WIN32_LEAN_AND_MEAN
#include "../tooling/windows_handle.h"
#include <windows.h>
#include "file_publication.h"
#include "../tooling/fnv1a.h"
#include "../tooling/windows_file_write.h"
#include "../tooling/windows_path.h"
#include "metadata_update_core.h"
#include <algorithm>
#include <cstring>
#include <vector>
#include <new>
namespace history {
#ifdef FLOGGFY_PUBLICATION_TEST
#define PUBLICATION_CHECKPOINT(phase, path) PublicationTestPhase(phase, path)
#else
#define PUBLICATION_CHECKPOINT(phase, path) ((void)0)
#endif
namespace {
using win32::Handle;
HANDLE OpenLockedFile(const std::wstring& path,DWORD access=GENERIC_READ|DELETE) {
    return CreateFileW(WindowsPath(path).c_str(),access,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
}
std::wstring UniqueSibling(const std::wstring& path,const wchar_t* label) {
    static volatile LONG sequence=0;
    return path+label+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(InterlockedIncrement(&sequence));
}
class StagingFile {
    Handle file_;
    bool published_=false;
public:
    StagingFile()=default;
    StagingFile(const StagingFile&)=delete;
    StagingFile& operator=(const StagingFile&)=delete;
    ~StagingFile() {
        if(file_.value!=INVALID_HANDLE_VALUE && !published_)
            win32::DeleteOnClose(file_.value);
    }
    bool WriteNew(const std::wstring& path,const void* bytes,size_t size) {
        if(!bytes || !size || size>MAXDWORD)return false;
        file_.value=CreateFileW(WindowsPath(path).c_str(),GENERIC_WRITE|DELETE,0,nullptr,
                                CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        return file_.value!=INVALID_HANDLE_VALUE && WriteAllBytes(file_.value,bytes,size) && FlushFileBuffers(file_.value);
    }
    bool Publish(const std::wstring& path) {
        published_=RenameWithoutReplacing(file_.value,path);
        return published_;
    }
};
bool SameFile(HANDLE handle,const std::wstring& path) {
    Handle current{CreateFileW(WindowsPath(path).c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    BY_HANDLE_FILE_INFORMATION a={},b={};
    return current.value!=INVALID_HANDLE_VALUE && GetFileInformationByHandle(handle,&a) &&
        GetFileInformationByHandle(current.value,&b) && a.dwVolumeSerialNumber==b.dwVolumeSerialNumber &&
        a.nFileIndexHigh==b.nFileIndexHigh && a.nFileIndexLow==b.nFileIndexLow;
}
struct PreviousRecording {Quality quality;bool complete=false;};
bool PrepareBackupSlot(const std::wstring& path,const Catalog& catalog,HANDLE old,const PreviousRecording& current) {
    const auto previous=path+L".previous";
    Handle backup{OpenLockedFile(previous)};
    if(backup.value==INVALID_HANDLE_VALUE) {
        const auto error=GetLastError();
        return win32::PathMissing(error);
    }
    // Reuse the inspection of the still-held target. Lock and validate the backup.
    RecordingFile retained;
    if(!SameFile(old,path)||!SameFile(backup.value,previous)||!ReadLockedRecording(backup.value,retained)||
       !retained.owned||!MatchesRecording(retained,catalog))return false;
    if(current.complete&&(!retained.complete||SameQuality(current.quality,retained.quality)||
                         HigherQuality(current.quality,retained.quality))) {
        return win32::DeleteOnClose(backup.value);
    }
    // Keep a better or incomparable old recording as an ordinary audio file.
    // Bounded no-overwrite renames cannot destroy an occupied recovery name.
    const auto dot=path.find_last_of(L'.');
    const auto stem=dot==std::wstring::npos?path:path.substr(0,dot);
    const auto extension=dot==std::wstring::npos?std::wstring():path.substr(dot);
    for(unsigned attempt=0;attempt<32;++attempt) {
        const auto target=stem+L" [recovered"+(attempt?L"-"+std::to_wstring(attempt):L"")+L"]"+extension;
        if(RenameWithoutReplacing(backup.value,target))return true;
        const auto error=GetLastError();
        if(error!=ERROR_ALREADY_EXISTS && error!=ERROR_FILE_EXISTS)return false;
    }
    return false;
}
FilePublication PromoteReplacement(const std::wstring& path,StagingFile& staging,const Catalog& catalog,HANDLE old,
                                   const PreviousRecording& current) {
    const auto backup=path+L".previous";
    if(!PrepareBackupSlot(path,catalog,old,current) || !RenameWithoutReplacing(old,backup))return FilePublication::Failed;
    PUBLICATION_CHECKPOINT(L"reserved",path);
    if(!staging.Publish(path)) {
        RenameWithoutReplacing(old,path); // Preserve any file created since reservation.
        return FilePublication::Failed;
    }
    win32::DeleteOnClose(old);
    return FilePublication::Upgraded;
}
}
static bool ArchiveFileMatches(HANDLE file,const void* bytes,size_t size) {
    LARGE_INTEGER actual{},zero{};
    if(!GetFileSizeEx(file,&actual)||actual.QuadPart<0)return false;
    if(uint64_t(actual.QuadPart)!=size){SetLastError(ERROR_SUCCESS);return false;}
    if(!SetFilePointerEx(file,zero,nullptr,FILE_BEGIN))return false;
    uint8_t chunk[16384];const auto* expected=static_cast<const uint8_t*>(bytes);
    for(size_t at=0;at<size;) {
        DWORD read=0;const auto count=DWORD(std::min(sizeof(chunk),size-at));
        if(!ReadFile(file,chunk,count,&read,nullptr))return false;
        if(read!=count){SetLastError(ERROR_HANDLE_EOF);return false;}
        if(memcmp(chunk,expected+at,count)){SetLastError(ERROR_SUCCESS);return false;}
        at+=count;
    }
    return true;
}
bool VerifiedArchiveFile(const std::wstring& path,const void* bytes,size_t size,bool remove,void** held) {
    auto file=OpenLockedFile(path,GENERIC_READ|(remove?DELETE:0));
    if(file==INVALID_HANDLE_VALUE) {
        const auto error=GetLastError();return remove&&win32::PathMissing(error);
    }
    if(!ArchiveFileMatches(file,bytes,size)) {
        // A readable changed object is foreign and preserved. I/O failures keep
        // cleanup pending rather than authorizing acknowledgement.
        const auto error=GetLastError();CloseHandle(file);
        return remove&&error==ERROR_SUCCESS;
    }
    bool valid=true;
    if(remove)valid=win32::DeleteOnClose(file);
    if(valid&&held){*held=file;return true;}
    CloseHandle(file);return valid;
}
bool WriteArchiveFile(const std::wstring& destination,const void* bytes,size_t size) {
    if(VerifiedArchiveFile(destination,bytes,size))return true;
    const auto stage=destination+L".partial";
    Handle file;
    file.value=CreateFileW(WindowsPath(stage).c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,FILE_SHARE_READ,nullptr,
        CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    const bool created=file.value!=INVALID_HANDLE_VALUE;
    if(!created) {
        file.value=OpenLockedFile(stage);
        if(file.value==INVALID_HANDLE_VALUE||!ArchiveFileMatches(file.value,bytes,size))return false;
    } else if(!WriteAllBytes(file.value,bytes,size)||!FlushFileBuffers(file.value)||!ArchiveFileMatches(file.value,bytes,size)) {
        win32::DeleteOnClose(file.value);
        return false;
    }
    PUBLICATION_CHECKPOINT(L"archive-stage-held",stage);
    // Both fresh and recovered stages remain held from verification through the
    // no-overwrite rename. A raced pathname never selects a different object.
    return RenameWithoutReplacing(file.value,destination);
}
FilenameConversion ConvertRecordingFilename(const std::wstring& source,const std::wstring& title_path,
    const Catalog& catalog,std::wstring& renamed_path,FilenameRenamePermit is_current,void* context) {
    try {
        auto same=[](const std::wstring& a,const std::wstring& b) {
            return CompareStringOrdinal(a.c_str(),int(a.size()),b.c_str(),int(b.size()),TRUE)==CSTR_EQUAL;
        };
        const auto candidates=RecordingPathCandidates(title_path,catalog);
        auto contains=[&](const std::wstring& name,bool writable_only) {
            return std::any_of(candidates.begin(),candidates.end(),[&](const auto& item) {
                return (!writable_only||item.writable)&&same(item.path,name);
            });
        };
        if(contains(source,true)||!contains(source,false))
            return FilenameConversion::Unchanged;
        const auto slash=source.find_last_of(L"\\/");
        if(slash==std::wstring::npos)return FilenameConversion::Unchanged;
        const auto filename=source.substr(slash+1);
        const auto track=TrackNumberPrefix(catalog.track);
        std::vector<std::wstring> prefixes;
        if(catalog.disc>1)prefixes.push_back(L"Disc "+std::to_wstring(catalog.disc)+L" - "+track);
        if(!track.empty())prefixes.push_back(track);
        std::wstring target;
        for(const auto& prefix:prefixes)if(filename.rfind(prefix,0)==0) {
            target=source.substr(0,slash+1)+filename.substr(prefix.size());break;
        }
        if(target.empty()||!contains(target,true))
            return FilenameConversion::Unchanged;

        // Freeze the exact source against writes/deletion before validating
        // ownership and identity. The existing rename helper never overwrites.
        if(is_current && !is_current(context))return FilenameConversion::Unchanged;
        Handle file{OpenLockedFile(source)};
        RecordingFile existing;
        if(file.value==INVALID_HANDLE_VALUE || !SameFile(file.value,source) ||
           !ReadLockedRecording(file.value,existing) || !existing.complete || !existing.owned ||
           !MatchesRecording(existing,catalog))return FilenameConversion::Failed;
        // Allocate the caller's result before committing the
        // pathname change, so allocation failure cannot hide a successful move.
        std::wstring result=target;
        // Inspection may have waited on slow storage. Recheck cancellation and
        // settings at commit, after allocation and before the atomic rename.
        if(is_current && !is_current(context))return FilenameConversion::Unchanged;
        if(!RenameWithoutReplacing(file.value,target)) {
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
    if(!SameQuality(incoming.quality,quality))return FilePublication::Failed;
    memory.SetRetainedBytes(RecordingMetadataMemoryUsage(incoming));
    RecoverRecording(path,catalog);
    if(memory.Denied())return FilePublication::ResourceWait;
    auto decision=DecideSave(path,quality,catalog);
    if(memory.Denied())return FilePublication::ResourceWait;
    if(decision==SaveDecision::Skip) return permit&&!permit(context)?FilePublication::Replan:FilePublication::Skipped;
    Handle old;
    PreviousRecording previous;
    std::vector<uint8_t> preserved;
    if(decision==SaveDecision::Upgrade) {
        // Freeze the exact existing object BEFORE composing replacement tags.
        // The same handle protects this snapshot through reservation/promotion.
        old.value=OpenLockedFile(path);
        std::vector<uint8_t> existing;
        RecordingFile current;
        if(old.value==INVALID_HANDLE_VALUE || !SameFile(old.value,path) ||
           !ReadLockedRecording(old.value,existing,current) ||
           !CanUpgradeRecording(current,quality,catalog))
            return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
        previous={current.quality,current.complete};
        if(current.complete) {
            if(RecordingReadMemoryScope::HasPermit()) {
                RecordingMemoryPlan old_memory;size_t peak=0;
                if(!ProbeRecordingMemory(existing.data(),existing.size(),old_memory)||
                   !EstimatePublicationUpgradeMemory(quality.codec,old_memory.file_bytes,size,
                       old_memory.metadata_bytes,incoming_memory.metadata_bytes,peak))return FilePublication::ResourceWait;
                if(!memory.Request(peak))return FilePublication::ResourceWait;
            }
            const auto first=static_cast<const uint8_t*>(bytes);
            const std::vector<uint8_t> replacement(first,first+size);
            std::string error;
            if(!PreserveUpgradeMetadata(existing,current,replacement,incoming,catalog,preserved,error))return FilePublication::Failed;
            bytes=preserved.data();size=preserved.size();
        }
        // Proven incomplete owned files are repaired from the new complete
        // capture; their unverified metadata cannot authorize preservation.
    }
    memory.SetRetainedBytes(RecordingMetadataMemoryUsage(incoming)+preserved.capacity());
    StagingFile staging;
    if(!staging.WriteNew(UniqueSibling(path,L".complete-"),bytes,size)) return FilePublication::Failed;
    PUBLICATION_CHECKPOINT(L"staged",path);
    if(permit&&!permit(context))return FilePublication::Replan;
    auto result=FilePublication::Failed;
    if(decision==SaveDecision::Upgrade) {
        // An interruption leaves .previous for recovery on the next lookup.
        result=PromoteReplacement(path,staging,catalog,old.value,previous);
        if(memory.Denied())return FilePublication::ResourceWait;
    } else if(staging.Publish(path)) {
        result=FilePublication::Saved;
    }
    if(published_bytes && (result==FilePublication::Saved || result==FilePublication::Upgraded))*published_bytes=size;
    return result;
    }catch(const std::bad_alloc&){return FilePublication::ResourceWait;}
    catch(...){return FilePublication::Failed;}
}

namespace {
uint64_t FileNumber(const BY_HANDLE_FILE_INFORMATION& info) {
    return (uint64_t(info.nFileIndexHigh)<<32)|info.nFileIndexLow;
}
bool ReceiptSourceUnchanged(HANDLE file,const FormatUpgradeReceipt& receipt,bool& unchanged) {
    unchanged=false;LARGE_INTEGER size{},zero{};
    if(!GetFileSizeEx(file,&size)||size.QuadPart<0)return false;
    if(uint64_t(size.QuadPart)!=receipt.source_bytes)return true;
    if(!SetFilePointerEx(file,zero,nullptr,FILE_BEGIN))return false;
    uint8_t bytes[16384];uint64_t hash=kFnv1aOffset;
    for(size_t at=0;at<receipt.source_bytes;) {
        const auto count=DWORD(std::min(sizeof(bytes),receipt.source_bytes-at));DWORD read=0;
        if(!ReadFile(file,bytes,count,&read,nullptr)||read!=count)return false;
        hash=Fnv1a64(bytes,count,hash);at+=count;
    }
    if(!SetFilePointerEx(file,zero,nullptr,FILE_BEGIN))return false;
    unchanged=hash==receipt.source_hash;return true;
}
bool ReadUpgradeDestination(HANDLE file,const FormatUpgradeReceipt& receipt,std::vector<uint8_t>& bytes,RecordingFile& recording) {
    if(file==INVALID_HANDLE_VALUE || !SameFile(file,receipt.destination) || !ReadLockedRecording(file,bytes,recording))return false;
    const auto& quality=recording.quality;
    return recording.complete && recording.owned && MatchesRecording(recording,receipt.catalog) &&
        HigherQuality(quality,receipt.source_quality) && (SameQuality(quality,receipt.quality) || HigherQuality(quality,receipt.quality));
}
FilePublication CompleteFormatUpgradeHeld(FormatUpgradeReceipt& receipt,HANDLE old,size_t* published_bytes) {
    RecordingReadMemoryScope memory;
    PUBLICATION_CHECKPOINT(L"format-final-read",receipt.destination);
    Handle final{OpenLockedFile(receipt.destination)};
    std::vector<uint8_t> final_bytes;RecordingFile recording;
    if(!ReadUpgradeDestination(final.value,receipt,final_bytes,recording))
        return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
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
        {
            RecordingFile prior;
            if(!ReadLockedRecording(old,old_bytes,prior)||
               !PreserveUpgradeMetadata(old_bytes,prior,final_bytes,recording,receipt.catalog,merged,error))
                return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
        }
        if(RecordingReadMemoryScope::HasPermit())
            memory.SetRetainedBytes(final_bytes.capacity()+old_bytes.capacity()+merged.capacity()+
                RecordingMetadataMemoryUsage(recording));
        if(merged!=final_bytes) {
            if(PublishMetadataBytes(receipt.destination,merged.data(),merged.size(),receipt.catalog,final.value)!=FilePublication::Upgraded)return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
            CloseHandle(final.value);final.value=OpenLockedFile(receipt.destination,GENERIC_READ);
            if(!ReadUpgradeDestination(final.value,receipt,final_bytes,recording))
                return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
        }
        receipt.metadata_preserved=true;
    }
    if(old!=INVALID_HANDLE_VALUE) {
        PUBLICATION_CHECKPOINT(L"format-old-delete",receipt.destination);
        if(!win32::DeleteOnClose(old))return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
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
        Handle old{OpenLockedFile(receipt.source)};
        if(old.value==INVALID_HANDLE_VALUE) {
            const auto error=GetLastError();
            if(!win32::PathMissing(error))return FilePublication::Failed;
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
           !MatchesRecording(next,catalog)||!SameQuality(next.quality,quality))return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
        memory.SetRetainedBytes(incoming.capacity()+RecordingMetadataMemoryUsage(next));
        Handle old{OpenLockedFile(source)};
        std::vector<uint8_t> old_bytes;RecordingFile prior;BY_HANDLE_FILE_INFORMATION old_info{};
        if(old.value==INVALID_HANDLE_VALUE||!SameFile(old.value,source)||!GetFileInformationByHandle(old.value,&old_info)||
           !ReadLockedRecording(old.value,old_bytes,prior)||
           !prior.complete||!prior.owned||prior.quality.codec!=Codec::Vorbis||!MatchesRecording(prior,catalog)||
           !HigherQuality(next.quality,prior.quality))return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
        // Allocate every receipt field before committing. A post-commit failure
        // cannot erase the original destination or send this job to a new root.
        FormatUpgradeReceipt prepared{source,destination,catalog,quality,prior.quality,FileNumber(old_info),old_info.dwVolumeSerialNumber,Fnv1a64(old_bytes.data(),old_bytes.size()),old_bytes.size(),false,false,false};
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
        if(!PreserveUpgradeMetadata(old_bytes,prior,incoming,next,catalog,preserved,error))return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
        const auto parsed_memory=RecordingMetadataMemoryUsage(next)+RecordingMetadataMemoryUsage(prior);
        memory.SetRetainedBytes(incoming.capacity()+old_bytes.capacity()+preserved.capacity()+parsed_memory);
        const auto result=PublishBytes(destination,quality,preserved.data(),preserved.size(),catalog,nullptr,permit,context);
        if(result==FilePublication::Failed||result==FilePublication::Replan||result==FilePublication::ResourceWait)return result;
        if(result==FilePublication::Skipped) {
            // An existing destination must satisfy the requested identity and
            // quality before it can authorize a commit receipt.
            Handle current{OpenLockedFile(destination,GENERIC_READ)};
            std::vector<uint8_t> current_bytes;RecordingFile recording;
            if(!ReadUpgradeDestination(current.value,receipt,current_bytes,recording))
                return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
        } else receipt.metadata_preserved=true;
        receipt.committed=true;
        // Close the large construction snapshots before the verification pass.
        std::vector<uint8_t>().swap(incoming);std::vector<uint8_t>().swap(old_bytes);std::vector<uint8_t>().swap(preserved);
        memory.SetRetainedBytes(parsed_memory);
        return CompleteFormatUpgradeHeld(receipt,old.value,published_bytes);
    }catch(const std::bad_alloc&){return FilePublication::ResourceWait;}
    catch(...){return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;}
}
FilePublication PublishMetadataBytes(const std::wstring& path,const void* bytes,size_t size,const Catalog& catalog,void* locked_file) {
    RecordingReadMemoryScope memory;RecordingFile incoming,current;
    if(!locked_file || locked_file==INVALID_HANDLE_VALUE || !SameFile(locked_file,path) ||
       !ReadLockedRecording(locked_file,current) || !current.complete || !current.owned || !MatchesRecording(current,catalog))
        return memory.Denied()?FilePublication::ResourceWait:FilePublication::Failed;
    memory.SetRetainedBytes(RecordingMetadataMemoryUsage(current));
    if(RecordingReadMemoryScope::HasPermit()) {
        RecordingMemoryPlan input;
        if(!ProbeRecordingMemory(static_cast<const uint8_t*>(bytes),size,input)||
           !memory.Request(input.inspection_peak-size))return FilePublication::ResourceWait;
    }
    if(!InspectRecording(static_cast<const uint8_t*>(bytes),size,incoming) || !incoming.complete || !incoming.owned ||
       !MatchesRecording(incoming,catalog)) return FilePublication::Failed;
    if(!SameQuality(current.quality,incoming.quality))return FilePublication::Failed;
    memory.SetRetainedBytes(RecordingMetadataMemoryUsage(current)+RecordingMetadataMemoryUsage(incoming));
    StagingFile staging;
    if(!staging.WriteNew(UniqueSibling(path,L".complete-lyrics-"),bytes,size))return FilePublication::Failed;
    PUBLICATION_CHECKPOINT(L"staged",path);
    const auto result=PromoteReplacement(path,staging,catalog,locked_file,{current.quality,current.complete});
    return memory.Denied()?FilePublication::ResourceWait:result;
}
}

#undef PUBLICATION_CHECKPOINT
