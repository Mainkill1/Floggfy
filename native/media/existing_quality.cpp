#define WIN32_LEAN_AND_MEAN
#include "../tooling/windows_handle.h"
#include <windows.h>
#include "existing_quality.h"
#include "../tooling/windows_file_read.h"
#include "../tooling/windows_path.h"
#include "../tooling/windows_file_write.h"
#include <algorithm>
#include <string_view>
#include <vector>
#include <utility>
namespace history {
namespace {
using win32::Handle;
std::wstring LookupPath(std::wstring path) {
    std::replace(path.begin(),path.end(),L'/',L'\\');
    return WindowsPath(path);
}
std::wstring ContainingDirectory(const std::wstring& file) {
    auto path=LookupPath(file);
    const auto slash=path.find_last_of(L'\\');
    if(slash==std::wstring::npos)return L".\\";
    path.resize(slash+1);
    return path;
}
bool FilenameLess(std::wstring_view a,std::wstring_view b) {
    return CompareStringOrdinal(a.data(),int(a.size()),b.data(),int(b.size()),TRUE)==CSTR_LESS_THAN;
}
std::wstring ArtistFolderAlias(const std::wstring& base,const Catalog& catalog) {
    if((catalog.kind!=MediaKind::Music && catalog.kind!=MediaKind::VariousArtists) ||
       (catalog.folder_artist.empty() && catalog.legacy_artist.empty()))return {};
    const auto slash=base.find_last_of(L"\\/"),dot=base.find_last_of(L'.');
    if(dot==std::wstring::npos || (slash!=std::wstring::npos && dot<slash))return {};
    const auto extension=base.substr(dot);
    // RelativePath owns sanitizing/truncation. Compare only its artist/album
    // directory suffix, so numbered and title-only filenames use the same alias.
    auto current=RelativePath(catalog,extension);
    auto enriched=catalog;enriched.folder_artist.clear();
    if(!catalog.legacy_artist.empty())enriched.artist=catalog.legacy_artist;
    enriched.kind=_wcsicmp(enriched.artist.c_str(),L"Various Artists")==0?MediaKind::VariousArtists:MediaKind::Music;
    auto alias=RelativePath(enriched,extension);
    const auto prefix=std::wstring(L"Music\\").size();
    current=current.substr(prefix,current.find_last_of(L'\\')+1-prefix);
    alias=alias.substr(prefix,alias.find_last_of(L'\\')+1-prefix);
    auto path=base;std::replace(path.begin(),path.end(),L'/',L'\\');
    const auto filename=path.find_last_of(L'\\');
    if(filename==std::wstring::npos || filename+1<current.size())return {};
    const auto start=filename+1-current.size();
    if(start && path[start-1]!=L'\\')return {};
    if(CompareStringOrdinal(path.c_str()+start,int(current.size()),current.c_str(),int(current.size()),TRUE)!=CSTR_EQUAL ||
       CompareStringOrdinal(current.c_str(),int(current.size()),alias.c_str(),int(alias.size()),TRUE)==CSTR_EQUAL)return {};
    return path.substr(0,start)+alias+path.substr(filename+1);
}
bool Missing(const std::wstring& path) {
    if(GetFileAttributesW(WindowsPath(path).c_str())!=INVALID_FILE_ATTRIBUTES) return false;
    DWORD error=GetLastError();
    return win32::PathMissing(error);
}
bool RestoreBackup(const std::wstring& backup,const std::wstring& destination,const Catalog& catalog) {
    // Bind recovery to the verified object, so another process cannot replace
    // the backup between inspection and rename.
    Handle file{CreateFileW(WindowsPath(backup).c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    RecordingFile old;
    if(!ReadLockedRecording(file.value,old) || !old.owned || !MatchesRecording(old,catalog)) return false;
    return RenameWithoutReplacing(file.value, destination);
}
}
const RecordingDirectory::Listing* RecordingDirectory::FindListing(const std::wstring& file) const {
    if(listings_.empty())return nullptr;
    const auto directory=ContainingDirectory(file);
    const auto listing=std::find_if(listings_.begin(),listings_.end(),[&](const auto& listing) {
        return CompareStringOrdinal(listing.directory.c_str(),int(listing.directory.size()),
            directory.c_str(),int(directory.size()),TRUE)==CSTR_EQUAL;
    });
    return listing==listings_.end()?nullptr:&*listing;
}
bool RecordingDirectory::Covers(const std::wstring& file) const {
    return FindListing(file)!=nullptr;
}
bool RecordingDirectory::Contains(const std::wstring& file) const {
    const auto* listing=FindListing(file);
    if(!listing)return false;
    const auto slash=file.find_last_of(L"\\/");
    const auto name=std::wstring_view(file).substr(slash==std::wstring::npos?0:slash+1);
    return std::binary_search(listing->names.begin(),listing->names.end(),name,FilenameLess);
}
bool ListRecordingDirectory(const std::wstring& file,RecordingDirectory& out,const Catalog* catalog) {
    out={};
    if(file.empty())return false;
    try {
        RecordingDirectory listing;
        std::vector<std::wstring> files{file};
        if(catalog) {auto alias=ArtistFolderAlias(file,*catalog);if(!alias.empty())files.push_back(std::move(alias));}
        for(const auto& lookup:files) {
            RecordingDirectory::Listing directory;
            directory.directory=ContainingDirectory(lookup);
            WIN32_FIND_DATAW entry{};
            win32::FindHandle find{FindFirstFileExW((directory.directory+L"*").c_str(),FindExInfoBasic,
                &entry,FindExSearchNameMatch,nullptr,0)};
            if(find.value==INVALID_HANDLE_VALUE) {
                const auto error=GetLastError();
                if(!win32::PathMissing(error))return false;
            } else {
                constexpr size_t max_entries=16384;
                size_t entries=0;
                for(;;) {
                    if(++entries>max_entries)return false;
                    const std::wstring name=entry.cFileName;
                    // Keep directories too: one occupying a candidate must not be
                    // mistaken for a vacant filename. Never recurse into them.
                    if(name!=L"." && name!=L"..")directory.names.push_back(name);
                    if(FindNextFileW(find.value,&entry))continue;
                    if(GetLastError()!=ERROR_NO_MORE_FILES)return false;
                    break;
                }
            }
            std::sort(directory.names.begin(),directory.names.end(),FilenameLess);
            listing.listings_.push_back(std::move(directory));
        }
        out=std::move(listing);
        return true;
    }catch(...) {return false;}
}
namespace {
bool PlanRecordingMemory(HANDLE file,size_t size,RecordingMemoryPlan& plan) {
    const bool result=ProbeRecordingMemory(size,[&](size_t offset,uint8_t* bytes,size_t count) {
        LARGE_INTEGER at{};at.QuadPart=static_cast<LONGLONG>(offset);DWORD copied=0;
        return SetFilePointerEx(file,at,nullptr,FILE_BEGIN)&&ReadFile(file,bytes,DWORD(count),&copied,nullptr)&&copied==count;
    },plan);
    LARGE_INTEGER zero{};return SetFilePointerEx(file,zero,nullptr,FILE_BEGIN)&&result;
}
RecordingRead InspectLockedRecording(HANDLE file,std::vector<uint8_t>& bytes,RecordingFile& out) {
    out={};
    LARGE_INTEGER size{};
    if(!file||file==INVALID_HANDLE_VALUE||!GetFileSizeEx(file,&size))return RecordingRead::ReadError;
    if(size.QuadPart<4||size.QuadPart>static_cast<LONGLONG>(kMaximumRecordingBytes))return RecordingRead::InvalidContainer;
    if(RecordingReadMemoryScope::HasPermit()) {
        RecordingMemoryPlan memory;
        if(!PlanRecordingMemory(file,size_t(size.QuadPart),memory))return RecordingRead::InvalidContainer;
        if(!RecordingReadMemoryScope::AdmitCurrent(memory.inspection_peak))return RecordingRead::ReadError;
    }
    if(!win32::ReadFileBytes(file,size_t(size.QuadPart),bytes))return RecordingRead::ReadError;
    RecordingFile inspected;
    if(!InspectRecording(bytes.data(),bytes.size(),inspected))
        return inspected.inspection_resource_failure?RecordingRead::ReadError:RecordingRead::InvalidContainer;
    out=std::move(inspected);
    return out.complete?RecordingRead::ValidComplete:RecordingRead::Incomplete;
}
bool ParsedRecording(RecordingRead result) {
    return result==RecordingRead::ValidComplete||result==RecordingRead::Incomplete;
}
}
bool PlanLockedRecordingMemory(void* file,RecordingMemoryPlan& plan) {
    plan={};LARGE_INTEGER size{};
    return file&&file!=INVALID_HANDLE_VALUE&&GetFileSizeEx(file,&size)&&size.QuadPart>=4&&
           size.QuadPart<=static_cast<LONGLONG>(kMaximumRecordingBytes)&&PlanRecordingMemory(file,size_t(size.QuadPart),plan);
}
bool ReadLockedRecording(void* file,std::vector<uint8_t>& bytes,RecordingFile& out) {
    return ParsedRecording(InspectLockedRecording(file,bytes,out));
}
bool ReadLockedRecording(void* file,RecordingFile& out) noexcept {
    std::vector<uint8_t> bytes;
    try {return ReadLockedRecording(file,bytes,out);}
    catch(...) {return false;}
}
RecordingRead InspectRecordingFile(const std::wstring& path,RecordingFile& out) {
    out={};
    try {
        // Deny writes while inspecting. Checked reads avoid mapped-file page
        // faults escaping C++ exception handling on unavailable storage. The
        // owned buffer is bounded to 1 GiB and is never used on capture paths.
        Handle file{CreateFileW(WindowsPath(path).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
        std::vector<uint8_t> bytes;
        return InspectLockedRecording(file.value,bytes,out);
    }catch(...) {
        // Allocation/parser failures leave no trusted quality or partial tags.
        return RecordingRead::ReadError;
    }
}
bool ReadRecording(const std::wstring& path,RecordingFile& out) {
    return ParsedRecording(InspectRecordingFile(path,out));
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
std::vector<RecordingPathCandidate> RecordingPathCandidates(const std::wstring& base,const Catalog& catalog,bool track_numbers) {
    if(base.empty()) return {};
    auto slash=base.find_last_of(L"\\/"),dot=base.find_last_of(L'.');
    if(dot==std::wstring::npos || (slash!=std::wstring::npos && dot<slash)) dot=base.size();
    const auto extension=base.substr(dot);
    const auto filename=slash==std::wstring::npos?0:slash+1;
    const auto directory=base.substr(0,filename);
    auto title=base.substr(filename);
    const auto track=TrackNumberPrefix(catalog.track);
    if(track_numbers && !track.empty()) {
        if(title.rfind(track,0)!=0)return {}; // A configured base must agree with its naming mode.
        title.erase(0,track.size());
    }
    const auto numbered=track+title;
    std::vector<RecordingPathCandidate> candidates{{base,true}};
    auto add_alias=[&](std::wstring path) {
        const auto same=[&](const auto& candidate){return candidate.path==path;};
        if(std::none_of(candidates.begin(),candidates.end(),same))candidates.push_back({std::move(path),false});
    };
    // Older numbered names remain valid homes for the same recording. Merely
    // changing filename style must never allocate a second copy of that song.
    auto add_folder=[&](const std::wstring& folder) {
        add_alias(folder+title);add_alias(folder+numbered);
        if(catalog.disc>1)add_alias(folder+L"Disc "+std::to_wstring(catalog.disc)+L" - "+numbered);
    };
    add_folder(directory);
    const auto artist_alias=ArtistFolderAlias(base,catalog);
    if(!artist_alias.empty())add_folder(artist_alias.substr(0,artist_alias.find_last_of(L"\\/")+1));

    std::wstring suffixes[]{RecordingSuffix(catalog),{}};
    if(!catalog.spotify_uri.empty()) {
        auto legacy=catalog;legacy.spotify_uri.clear();
        if(auto suffix=RecordingSuffix(legacy);suffix!=suffixes[0])suffixes[1]=std::move(suffix);
    }
    const auto base_count=candidates.size();
    constexpr size_t attempts=32;
    candidates.reserve(base_count*(1+attempts*(suffixes[1].empty()?1:2)));
    for(size_t i=0;i<base_count;++i) {
        const auto& ordinary=candidates[i].path;
        const auto stem=ordinary.substr(0,ordinary.size()-extension.size());
        for(const auto& suffix:suffixes) {
            if(suffix.empty())break;
            const auto named=stem+suffix;
            for(size_t attempt=0;attempt<attempts;++attempt) {
                std::wstring path;
                path.reserve(named.size()+3+extension.size());
                path+=named;
                if(attempt)path+=L'-'+std::to_wstring(attempt);
                path+=extension;
                // The fixed-size hash and optional numeric tail distinguish all
                // generated names. Only the original base names need a check.
                const auto end=candidates.begin()+base_count;
                const auto existing=std::find_if(candidates.begin(),end,[&](const auto& item){return item.path==path;});
                if(existing==end)candidates.push_back({std::move(path),i==0});
                else if(i==0)existing->writable=true;
            }
        }
    }

    return candidates;
}
RecordingDestination ResolveRecordingDestination(const std::wstring& base,const Catalog& catalog,bool track_numbers,
    const RecordingDirectory* directory) {
    RecordingReadMemoryScope memory;
    const auto candidates=RecordingPathCandidates(base,catalog,track_numbers);
    if(candidates.empty())return {};
    RecordingDirectory local;
    if(!directory) {
        if(!ListRecordingDirectory(base,local,&catalog))return {};
        directory=&local;
    }
    if(!directory->Covers(base))return {};

    // Never return the first vacancy until all deterministic names have been
    // compared in RAM with this directory listing. Only listed candidates are
    // opened. A prior collision remains this recording's home even if the
    // unrelated occupant of the ordinary filename was later deleted.
    std::wstring vacant,repairable,matching;
    bool unreadable=false,explicit_uid_conflict=false;
    RecordingFile best_recording,repairable_recording;
    for(const auto& [path,writable]:candidates) {
        if(!directory->Contains(path)) {
            // Historical prefixes are lookup aliases, never new-file names.
            if(vacant.empty()&&writable)vacant=path;
            continue;
        }
        memory.SetRetainedBytes(RecordingMetadataMemoryUsage(best_recording)+RecordingMetadataMemoryUsage(repairable_recording));
        RecordingFile existing;
        const auto result=InspectRecordingFile(path,existing);
        if(result==RecordingRead::ReadError) {
            unreadable=unreadable || !Missing(path);
            continue;
        }
        if(result!=RecordingRead::InvalidContainer && ValidSpotifyTrackUri(catalog.spotify_uri)) {
            const auto uid=std::find_if(existing.tags.fields.begin(),existing.tags.fields.end(),[](const auto& field) {
                return field.first=="SPOTIFY_URI";
            });
            if(uid!=existing.tags.fields.end() && ValidSpotifyTrackUri(uid->second) && uid->second!=catalog.spotify_uri)
                explicit_uid_conflict=true;
        }
        if(result==RecordingRead::InvalidContainer || !MatchesRecording(existing,catalog)) continue;
        if(existing.complete) {
            // If legacy duplicates already exist, retain the best verified
            // audio. Equal/incomparable qualities use stable candidate order.
            if(matching.empty() || HigherQuality(existing.quality,best_recording.quality)) {
                matching=path;
                best_recording=std::move(existing);
            }
        } else if(existing.owned && repairable.empty()) {
            repairable=path;
            repairable_recording=std::move(existing);
        }
    }
    if(!matching.empty()) {
        const auto selected_uid=std::find_if(best_recording.tags.fields.begin(),best_recording.tags.fields.end(),[](const auto& field) {
            return field.first=="SPOTIFY_URI";
        });
        // A legacy identity-less recording remains on the established path.
        // Only an exact selected UID keeps the separate collision observation.
        if(selected_uid==best_recording.tags.fields.end() || selected_uid->second!=catalog.spotify_uri)
            explicit_uid_conflict=false;
        return {RecordingDestinationState::Existing,std::move(matching),std::move(best_recording),explicit_uid_conflict};
    }
    // An inaccessible candidate may be the already-saved recording. Retry on
    // a later listen rather than allocating a possible duplicate beside it.
    if(unreadable) return {};
    // Only recover backups if no complete candidate already exists. Avoid
    // restoring an obsolete ordinary filename beside a retained suffix.
    bool restore_reserved=false;
    for(const auto& candidate:candidates) {
        const auto& path=candidate.path;
        const auto backup=path+L".previous";
        if(!directory->Contains(backup))continue;
        memory.SetRetainedBytes(RecordingMetadataMemoryUsage(best_recording)+RecordingMetadataMemoryUsage(repairable_recording));
        RecordingFile retained;
        const auto result=InspectRecordingFile(backup,retained);
        if(result==RecordingRead::ReadError) {
            if(!Missing(backup))return {};
            continue;
        }
        if(result==RecordingRead::InvalidContainer || !retained.owned || !MatchesRecording(retained,catalog))continue;
        if(directory->Contains(path) && !Missing(path)) {
            if(path==base)restore_reserved=true;
            else return {}; // Retained suffix backup needs recovery, not a duplicate.
            continue;
        }
        memory.SetRetainedBytes(RecordingMetadataMemoryUsage(best_recording)+RecordingMetadataMemoryUsage(repairable_recording)+RecordingMetadataMemoryUsage(retained));
        if(!RestoreBackup(backup,path,catalog))return {};
        RecordingFile restored;
        if(ReadRecording(path,restored) && restored.owned && MatchesRecording(restored,catalog)) {
            if(restored.complete)return {RecordingDestinationState::Existing,path,std::move(restored),explicit_uid_conflict};
            if(restored.owned && repairable.empty()) {repairable=path;repairable_recording=std::move(restored);}
        } else return {};
    }
    if(!repairable.empty()) return {RecordingDestinationState::Existing,std::move(repairable),std::move(repairable_recording),explicit_uid_conflict};

    const auto backup=base+L".previous";
    if(restore_reserved) {
        // A raced ordinary destination can leave its verified old object in
        // .previous. Recover beside the raced file without overwriting it.
        for(const auto& [path,writable]:candidates) {
            if(!writable||directory->Contains(path))continue;
            const bool recovered=RestoreBackup(backup,path,catalog);
            RecordingFile restored;
            if(!ReadRecording(path,restored)||!MatchesRecording(restored,catalog)) {
                if(recovered)return {};
                continue;
            }
            if(recovered||restored.complete||restored.owned)
                return {RecordingDestinationState::Existing,path,std::move(restored),explicit_uid_conflict};
        }
        return {}; // Do not bypass an existing recording whose recovery failed.
    }
    if(vacant.empty()) return {};
    return {RecordingDestinationState::Vacant,std::move(vacant),{},explicit_uid_conflict};
}
SaveDecision DecideSave(const std::wstring& path,const Quality& incoming,const Catalog& catalog) {
    if(Missing(path)) return SaveDecision::NewFile;
    RecordingFile existing;
    return ReadRecording(path,existing) && CanUpgradeRecording(existing,incoming,catalog)
        ? SaveDecision::Upgrade : SaveDecision::Skip;
}
}
