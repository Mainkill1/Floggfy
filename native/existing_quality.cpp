#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "existing_quality.h"
#include "history_settings.h"
#include <cstring>
#include <algorithm>
#include <vector>
#include <utility>
#include <cwchar>
namespace history {
namespace {
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~Handle() { if(value!=INVALID_HANDLE_VALUE && value) CloseHandle(value); }
};
struct FindHandle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~FindHandle(){if(value!=INVALID_HANDLE_VALUE)FindClose(value);}
};
std::wstring LookupPath(std::wstring path) {
    std::replace(path.begin(),path.end(),L'/',L'\\');
    return WindowsPath(path);
}
std::wstring ContainingDirectory(const std::wstring& file) {
    const auto path=LookupPath(file);
    const auto slash=path.find_last_of(L'\\');
    return slash==std::wstring::npos?L".\\":path.substr(0,slash+1);
}
bool FilenameLess(const std::wstring& a,const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(),int(a.size()),b.c_str(),int(b.size()),TRUE)==CSTR_LESS_THAN;
}
std::wstring ArtistFolderAlias(const std::wstring& base,const Catalog& catalog) {
    if((catalog.kind!=MediaKind::Music && catalog.kind!=MediaKind::VariousArtists) || catalog.folder_artist.empty())return {};
    const auto slash=base.find_last_of(L"\\/"),dot=base.find_last_of(L'.');
    if(dot==std::wstring::npos || (slash!=std::wstring::npos && dot<slash))return {};
    const auto extension=base.substr(dot);
    // RelativePath owns sanitizing/truncation. Compare only its artist/album
    // directory suffix, so numbered and title-only filenames use the same alias.
    auto current=RelativePath(catalog,extension);
    auto enriched=catalog;enriched.folder_artist.clear();
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
bool RecordingDirectory::Covers(const std::wstring& file) const {
    const auto directory=ContainingDirectory(file);
    return complete_ && std::any_of(listings_.begin(),listings_.end(),[&](const auto& listing) {
        return CompareStringOrdinal(listing.directory.c_str(),int(listing.directory.size()),
            directory.c_str(),int(directory.size()),TRUE)==CSTR_EQUAL;
    });
}
bool RecordingDirectory::Contains(const std::wstring& file) const {
    if(!complete_)return false;
    const auto directory=ContainingDirectory(file);
    const auto slash=file.find_last_of(L"\\/");
    const auto name=file.substr(slash==std::wstring::npos?0:slash+1);
    for(const auto& listing:listings_) {
        if(CompareStringOrdinal(listing.directory.c_str(),int(listing.directory.size()),
           directory.c_str(),int(directory.size()),TRUE)==CSTR_EQUAL)
            return std::binary_search(listing.names.begin(),listing.names.end(),name,FilenameLess);
    }
    return false;
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
            FindHandle find{FindFirstFileExW((directory.directory+L"*").c_str(),FindExInfoBasic,
                &entry,FindExSearchNameMatch,nullptr,0)};
            if(find.value==INVALID_HANDLE_VALUE) {
                const auto error=GetLastError();
                if(error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND)return false;
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
        listing.complete_=true;
        out=std::move(listing);
        return true;
    }catch(...) {return false;}
}
RecordingRead InspectRecordingFile(const std::wstring& path,RecordingFile& out) {
    out={};
    try {
        // Deny writes while inspecting. Checked reads avoid mapped-file page
        // faults escaping C++ exception handling on unavailable storage. The
        // owned buffer is bounded to 512 MiB and is never used on capture paths.
        Handle file{CreateFileW(WindowsPath(path).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
        if(file.value==INVALID_HANDLE_VALUE)return RecordingRead::ReadError;
        LARGE_INTEGER size={};
        if(!GetFileSizeEx(file.value,&size))return RecordingRead::ReadError;
        if(size.QuadPart<4||size.QuadPart>512LL*1024*1024)return RecordingRead::InvalidContainer;
        std::vector<uint8_t> bytes(size_t(size.QuadPart));
        constexpr size_t chunk=1024*1024;
        for(size_t offset=0;offset<bytes.size();) {
            const DWORD wanted=DWORD(std::min(chunk,bytes.size()-offset));DWORD read=0;
            if(!ReadFile(file.value,bytes.data()+offset,wanted,&read,nullptr)||read!=wanted)return RecordingRead::ReadError;
            offset+=read;
        }
        LARGE_INTEGER after={};
        if(!GetFileSizeEx(file.value,&after)||after.QuadPart!=size.QuadPart)return RecordingRead::ReadError;
        RecordingFile inspected;
        if(!InspectRecording(bytes.data(),bytes.size(),inspected))return RecordingRead::InvalidContainer;
        out=std::move(inspected);
        return out.complete?RecordingRead::ValidComplete:RecordingRead::Incomplete;
    }catch(...) {
        // Allocation/parser failures leave no trusted quality or partial tags.
        return RecordingRead::ReadError;
    }
}
bool ReadRecording(const std::wstring& path,RecordingFile& out) {
    const auto result=InspectRecordingFile(path,out);
    return result==RecordingRead::ValidComplete || result==RecordingRead::Incomplete;
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
std::vector<std::wstring> RecordingPathCandidates(const std::wstring& base,const Catalog& catalog,bool legacy_names,bool track_numbers) {
    if(base.empty()) return {};
    auto slash=base.find_last_of(L"\\/"),dot=base.find_last_of(L'.');
    if(dot==std::wstring::npos || (slash!=std::wstring::npos && dot<slash)) dot=base.size();
    const auto extension=base.substr(dot);
    const auto filename=slash==std::wstring::npos?0:slash+1;
    const auto directory=base.substr(0,filename);
    std::wstring title=base.substr(filename),track;
    if(catalog.track){wchar_t prefix[20];std::swprintf(prefix,20,L"%02u - ",catalog.track);track=prefix;}
    if(track_numbers && !track.empty()) {
        if(title.rfind(track,0)!=0)return {}; // A configured base must agree with its naming mode.
        title.erase(0,track.size());
    }
    const auto numbered=track+title;
    std::vector<std::wstring> bases{base};
    auto alias=[&](const std::wstring& folder,const std::wstring& name) {
        const auto path=folder+name;
        if(std::find(bases.begin(),bases.end(),path)==bases.end())bases.push_back(path);
    };
    if(legacy_names) {
        std::vector<std::wstring> folders{directory};
        const auto artist_alias=ArtistFolderAlias(base,catalog);
        if(!artist_alias.empty())folders.push_back(artist_alias.substr(0,artist_alias.find_last_of(L"\\/")+1));
        for(const auto& folder:folders) {
            alias(folder,title);alias(folder,numbered);
            if(catalog.disc>1)alias(folder,L"Disc "+std::to_wstring(catalog.disc)+L" - "+numbered);
        }
    }
    // Older numbered names remain valid homes for the same recording. Merely
    // changing filename style must never allocate a second copy of that song.
    auto candidates=bases;
    for(const auto& ordinary:bases) {
        const auto stem=ordinary.substr(0,ordinary.size()-extension.size());
        auto add_suffix=[&](const Catalog& identity) {
            const auto named=stem+RecordingSuffix(identity);
            for(unsigned attempt=0;attempt<32;++attempt) {
                const auto path=named+(attempt?L"-"+std::to_wstring(attempt):L"")+extension;
                if(std::find(candidates.begin(),candidates.end(),path)==candidates.end())candidates.push_back(path);
            }
        };
        add_suffix(catalog);
        if(!catalog.spotify_uri.empty()) {
            auto legacy=catalog;legacy.spotify_uri.clear();add_suffix(legacy);
        }
    }

    return candidates;
}
RecordingDestination ResolveRecordingDestination(const std::wstring& base,const Catalog& catalog,bool track_numbers,
    const RecordingDirectory* directory) {
    const auto candidates=RecordingPathCandidates(base,catalog,true,track_numbers);
    if(candidates.empty())return {};
    const auto writable=RecordingPathCandidates(base,catalog,false,track_numbers);
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
    bool unreadable=false;
    Quality best;
    RecordingFile best_recording,repairable_recording;
    for(const auto& path:candidates) {
        if(!directory->Contains(path)) {
            // Historical prefixes are lookup aliases, never new-file names.
            if(vacant.empty() && std::find(writable.begin(),writable.end(),path)!=writable.end()) vacant=path;
            continue;
        }
        RecordingFile existing;
        const auto result=InspectRecordingFile(path,existing);
        if(result==RecordingRead::ReadError) {
            unreadable=unreadable || !Missing(path);
            continue;
        }
        if(result==RecordingRead::InvalidContainer || !MatchesRecording(existing,catalog)) continue;
        if(existing.complete) {
            // If legacy duplicates already exist, retain the best verified
            // audio. Equal/incomparable qualities use stable candidate order.
            if(matching.empty() || HigherQuality(existing.quality,best)) {
                matching=path;
                best=existing.quality;
                best_recording=std::move(existing);
            }
        } else if(existing.owned && repairable.empty()) {
            repairable=path;
            repairable_recording=std::move(existing);
        }
    }
    if(!matching.empty()) return {RecordingDestinationState::Existing,std::move(matching),std::move(best_recording)};
    // An inaccessible candidate may be the already-saved recording. Retry on
    // a later listen rather than allocating a possible duplicate beside it.
    if(unreadable) return {};
    // Only recover backups if no complete candidate already exists. Avoid
    // restoring an obsolete ordinary filename beside a retained suffix.
    bool restore_reserved=false;
    for(const auto& path:candidates) {
        const auto backup=path+L".previous";
        if(!directory->Contains(backup))continue;
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
        if(!RestoreBackup(backup,path,catalog))return {};
        RecordingFile restored;
        if(ReadRecording(path,restored) && restored.owned && MatchesRecording(restored,catalog)) {
            if(restored.complete)return {RecordingDestinationState::Existing,path,std::move(restored)};
            if(restored.owned && repairable.empty()) {repairable=path;repairable_recording=std::move(restored);}
        } else return {};
    }
    if(!repairable.empty()) return {RecordingDestinationState::Existing,std::move(repairable),std::move(repairable_recording)};

    const auto backup=base+L".previous";
    if(restore_reserved) {
        // A raced ordinary destination can leave its verified old object in
        // .previous. Recover beside the raced file without overwriting it.
        for(const auto& path:writable) {
            if(directory->Contains(path)) continue;
            if(RestoreBackup(backup,path,catalog) || Reusable(path,catalog)) {
                RecordingFile restored;
                if(ReadRecording(path,restored) && MatchesRecording(restored,catalog))
                    return {RecordingDestinationState::Existing,path,std::move(restored)};
                return {};
            }
        }
        return {}; // Do not bypass an existing recording whose recovery failed.
    }
    if(vacant.empty()) return {};
    return {RecordingDestinationState::Vacant,std::move(vacant),{}};
}
std::wstring ResolveRecordingPath(const std::wstring& base,const Catalog& catalog,bool track_numbers) {
    return ResolveRecordingDestination(base,catalog,track_numbers).path;
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
