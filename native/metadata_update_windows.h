#pragma once
#include "library_layout.h"
#include "ogg_tags.h"
namespace history {
enum class FileMetadataUpdate { Updated, Skipped, Failed, InUse };
// Fill verified existing audio without changing quality/identity. Optional
// current-intent permit is checked again after staging and source reservation.
FileMetadataUpdate UpdateRecordingMetadata(const std::wstring& path,const Tags& incoming,const Catalog& catalog,unsigned expected_generation=~0u,
    bool(*is_current)(void*)=nullptr,void* context=nullptr);
}
