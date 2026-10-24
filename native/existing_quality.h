#pragma once
#include "library_layout.h"
#include "recording_file.h"
#include <string>
#include <vector>
namespace history {
// Complete container and supported codec-header validation, not independent
// audio decoding. A header alone never counts as a saved song.
bool ReadQuality(const std::wstring& file,Quality& out);
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
};
// Blocked distinguishes I/O uncertainty from a verified vacancy. Callers must
// not interpret it as absence when searching a preferred recording format.
RecordingDestination ResolveRecordingDestination(const std::wstring& base,const Catalog& catalog,bool track_numbers=false,
    const RecordingDirectory* directory=nullptr);
// Pure, bounded naming rule shared by publication and folder lookup. The menu
// can stop at its first complete matching file; replacement must examine all.
std::vector<std::wstring> RecordingPathCandidates(const std::wstring& base,const Catalog& catalog,bool legacy_names=true,bool track_numbers=false);
std::wstring ResolveRecordingPath(const std::wstring& base,const Catalog& catalog,bool track_numbers=false);
enum class SaveDecision { NewFile, Upgrade, Skip };
SaveDecision DecideSave(const std::wstring& path,const Quality& incoming);
// Optional inspection lets callers plan metadata changes without rereading the file.
SaveDecision DecideSave(const std::wstring& path,const Quality& incoming,const Catalog& catalog,RecordingFile* inspected=nullptr);
}
