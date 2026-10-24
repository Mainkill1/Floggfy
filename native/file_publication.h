#pragma once
#include "existing_quality.h"
#include <cstddef>
namespace history {
enum class FilePublication { Saved, Upgraded, Skipped, Failed };
enum class FilenameConversion { Unchanged, Renamed, Failed };
// Publication-worker-only: remove an exact old metadata-derived prefix from a
// complete owned recording. Atomic same-folder rename; never replaces a target.
// On Unchanged/Failed, renamed_path is untouched and the source is retained.
FilenameConversion ConvertRecordingFilename(const std::wstring& source,const std::wstring& title_path,
    const Catalog& catalog,std::wstring& renamed_path);
// Input is complete tagged audio. The catalog identifies this recording, not
// merely its sanitized filename. Every publication stages before rename.
FilePublication PublishBytes(const std::wstring& path,const Quality& quality,const void* bytes,size_t size,const Catalog& catalog,
                             size_t* published_bytes=nullptr);
// Caller retains a GENERIC_READ|DELETE handle denying writes/deletes from the
// original metadata read through this exact-object replacement.
FilePublication PublishMetadataBytes(const std::wstring& path,const void* bytes,size_t size,const Catalog& catalog,void* locked_file);
// Compatibility entry point derives identity from the incoming file's tags.
FilePublication PublishBytes(const std::wstring& path,const Quality& quality,const void* bytes,size_t size);
}
