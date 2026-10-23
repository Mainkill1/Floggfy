#pragma once
#include "library_layout.h"
#include "recording_file.h"
#include <string>
namespace history {
// Complete container validation; a header alone never counts as a saved song.
bool ReadQuality(const std::wstring& file,Quality& out);
bool ReadRecording(const std::wstring& file,RecordingFile& out);
bool ExistingRecording(const std::wstring& file,const Catalog& catalog,Quality& out);
// Empty means no safe destination could be established. No directories created.
std::wstring ResolveRecordingPath(const std::wstring& base,const Catalog& catalog);
enum class SaveDecision { NewFile, Upgrade, Skip };
SaveDecision DecideSave(const std::wstring& path,const Quality& incoming);
SaveDecision DecideSave(const std::wstring& path,const Quality& incoming,const Catalog& catalog);
}
