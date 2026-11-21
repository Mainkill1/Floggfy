#pragma once
#include "library_layout.h"
#include "recording_file.h"
#include "recording_memory.h"
#include <limits>
#include <string>
#include <vector>
namespace history {
using RecordingMemoryPermit=bool(*)(void*,size_t incremental_peak);
// A publication installs the root callback; nested scopes add their currently
// retained transaction buffers. Callback reservations must only grow until the
// outer operation returns. Denial remains visible through all enclosing scopes.
class RecordingReadMemoryScope {
    inline static thread_local RecordingReadMemoryScope* current_=nullptr;
    RecordingReadMemoryScope* previous_=nullptr;
    RecordingMemoryPermit permit_=nullptr;void* context_=nullptr;
    size_t retained_=0;bool denied_=false;
    void Deny() noexcept {for(auto* scope=this;scope;scope=scope->previous_)scope->denied_=true;}
public:
    explicit RecordingReadMemoryScope(RecordingMemoryPermit permit=nullptr,void* context=nullptr) noexcept
        :previous_(current_),permit_(permit),context_(context){current_=this;}
    ~RecordingReadMemoryScope(){current_=previous_;}
    RecordingReadMemoryScope(const RecordingReadMemoryScope&)=delete;
    RecordingReadMemoryScope& operator=(const RecordingReadMemoryScope&)=delete;
    bool Request(size_t peak) noexcept {
        if(peak>std::numeric_limits<size_t>::max()-retained_){Deny();return false;}
        const auto total=retained_+peak;bool allowed=false;
        try {allowed=permit_?permit_(context_,total):!previous_||previous_->Request(total);}
        catch(...) {allowed=false;}
        if(!allowed)Deny();
        return allowed;
    }
    void SetRetainedBytes(size_t bytes) noexcept {retained_=bytes;}
    bool Denied() const noexcept {return denied_;}
    static bool AdmitCurrent(size_t bytes) noexcept {return !current_||current_->Request(bytes);}
    static bool HasPermit() noexcept {
        for(auto* scope=current_;scope;scope=scope->previous_)if(scope->permit_)return true;
        return false;
    }
};
inline size_t RecordingMetadataMemoryUsage(const RecordingFile& file) noexcept {
    size_t bytes=sizeof(file)+file.tags.cover.capacity()+file.tags.mime.capacity()+
        file.tags.fields.capacity()*sizeof(file.tags.fields[0])+file.picture_comments.capacity()*sizeof(std::string)+
        file.artworks.capacity()*sizeof(RecordingArtwork)+file.artwork.mime.capacity();
    for(const auto& field:file.tags.fields)bytes+=field.first.capacity()+field.second.capacity();
    for(const auto& comment:file.picture_comments)bytes+=comment.capacity();
    for(const auto& artwork:file.artworks)bytes+=artwork.mime.capacity();
    return bytes;
}
// Header-only preflight against the held object; restores its position to zero.
bool PlanLockedRecordingMemory(void* locked_file,RecordingMemoryPlan& plan);
// Complete container and supported codec-header validation, not independent
// audio decoding. A header alone never counts as a saved song.
enum class RecordingRead { ValidComplete, Incomplete, InvalidContainer, ReadError };
// Separates a parsed but incomplete recording from an unreadable object. All
// error outcomes clear out; identity is checked separately by the caller.
RecordingRead InspectRecordingFile(const std::wstring& file,RecordingFile& out);
bool ReadRecording(const std::wstring& file,RecordingFile& out);
bool ExistingRecording(const std::wstring& file,const Catalog& catalog,Quality& out);
// Empty means no safe destination could be established. No directories created.
// Recover only a verified same-recording backup left by an interrupted upgrade.
bool RecoverRecording(const std::wstring& path,const Catalog& catalog);
enum class RecordingDestinationState { Vacant, Existing, Blocked };
// Short-lived, bounded listings of the containing directory and, when needed,
// its single known Spotify-artist alias. All codec/name
// checks in a worker operation share it; it is never a cross-request cache or
// authorization to rename/replace a file. Partial/failed listings are unusable.
class RecordingDirectory {
public:
    bool Covers(const std::wstring& file) const;
    bool Contains(const std::wstring& file) const;
private:
    friend bool ListRecordingDirectory(const std::wstring&,RecordingDirectory&,const Catalog*);
    struct Listing {std::wstring directory;std::vector<std::wstring> names;};
    std::vector<Listing> listings_;
    bool complete_=false;
};
// A missing directory is an empty listing; access/enumeration errors and more
// than 16,384 entries per directory fail closed. The optional catalog adds only
// its exact artist-folder alias, never a library scan. No directories created.
bool ListRecordingDirectory(const std::wstring& file,RecordingDirectory& out,const Catalog* catalog=nullptr);
struct RecordingDestination {
    RecordingDestinationState state=RecordingDestinationState::Blocked;
    std::wstring path;
    // Verified matching snapshot from the bounded search, not a reservation.
    // Publication must still lock/revalidate the exact object before writes.
    RecordingFile recording;
    // Observation only: a complete Spotify track UID on a listed candidate
    // differed from the requested complete UID. This never authorizes an
    // overwrite or changes the resolver's existing destination decision.
    bool explicit_uid_conflict=false;
};
// Blocked distinguishes I/O uncertainty from a verified vacancy. Callers must
// not interpret it as absence when searching a preferred recording format.
RecordingDestination ResolveRecordingDestination(const std::wstring& base,const Catalog& catalog,bool track_numbers=false,
    const RecordingDirectory* directory=nullptr);
// Pure, bounded naming rule shared by publication and folder lookup. The menu
// can stop at its first complete matching file; replacement must examine all.
std::vector<std::wstring> RecordingPathCandidates(const std::wstring& base,const Catalog& catalog,bool legacy_names=true,bool track_numbers=false);
enum class SaveDecision { NewFile, Upgrade, Skip };
// Optional inspection lets callers plan metadata changes without rereading the file.
SaveDecision DecideSave(const std::wstring& path,const Quality& incoming,const Catalog& catalog,RecordingFile* inspected=nullptr);
}
