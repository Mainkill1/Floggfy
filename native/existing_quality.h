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
struct RecordingDestination {
    RecordingDestinationState state=RecordingDestinationState::Blocked;
    std::wstring path;
    // Verified matching snapshot from the bounded search, not a reservation.
    // Publication must still lock/revalidate the exact object before writes.
    RecordingFile recording;
};
// Blocked distinguishes I/O uncertainty from a verified vacancy. Callers must
// not interpret it as absence when searching a preferred recording format.
RecordingDestination ResolveRecordingDestination(const std::wstring& base,const Catalog& catalog);
// Pure, bounded naming rule shared by publication and folder lookup. The menu
// can stop at its first complete matching file; replacement must examine all.
std::vector<std::wstring> RecordingPathCandidates(const std::wstring& base,const Catalog& catalog,bool legacy_names=true);
std::wstring ResolveRecordingPath(const std::wstring& base,const Catalog& catalog);
enum class SaveDecision { NewFile, Upgrade, Skip };
SaveDecision DecideSave(const std::wstring& path,const Quality& incoming);
// Optional inspection lets callers plan metadata changes without rereading the file.
SaveDecision DecideSave(const std::wstring& path,const Quality& incoming,const Catalog& catalog,RecordingFile* inspected=nullptr);
}
