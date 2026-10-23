#pragma once
#include "recording_file.h"
#include <string>
#include <vector>
namespace history {
enum class MetadataUpdate { Updated,Unchanged,Invalid };
// Fill safe missing metadata on a complete Floggfy-owned matching recording.
// Unchanged/Invalid clear output; source audio and populated metadata are kept.
MetadataUpdate FillRecordingMetadata(const std::vector<uint8_t>& source,const Tags& incoming,
    const Catalog& catalog,std::vector<uint8_t>& output,std::string& error);
}
