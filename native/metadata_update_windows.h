#pragma once
#include "library_layout.h"
#include "ogg_tags.h"
namespace history {
enum class FileMetadataUpdate { Updated, Skipped, Failed };
// Fill verified existing audio without changing its quality or recording identity.
FileMetadataUpdate UpdateRecordingMetadata(const std::wstring& path,const Tags& incoming,const Catalog& catalog,unsigned expected_generation=~0u);
}
