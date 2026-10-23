#pragma once
#include "library_layout.h"
#include "recording_file.h"
#include <string>
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
std::wstring ResolveRecordingPath(const std::wstring& base,const Catalog& catalog);
enum class SaveDecision { NewFile, Upgrade, Skip };
SaveDecision DecideSave(const std::wstring& path,const Quality& incoming);
// Optional inspection lets callers plan metadata changes without rereading the file.
SaveDecision DecideSave(const std::wstring& path,const Quality& incoming,const Catalog& catalog,RecordingFile* inspected=nullptr);
}
