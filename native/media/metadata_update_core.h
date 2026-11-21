#pragma once
#include "recording_file.h"
#include <string>
#include <vector>
namespace history {
enum class MetadataUpdate { Updated,Unchanged,Invalid };
// Shares the exact fill/cover plan without opening or rewriting a file.
bool NeedsRecordingMetadata(const RecordingFile& existing,const Tags& incoming,bool backfill_saved_date=false);
// Fill safe missing metadata on a complete Floggfy-owned matching recording.
// Unchanged/Invalid clear output; source audio and populated metadata are kept.
MetadataUpdate FillRecordingMetadata(const std::vector<uint8_t>& source,const Tags& incoming,
    const Catalog& catalog,std::vector<uint8_t>& output,std::string& error);
// Audio replacement policy: old populated/custom fields and artwork survive;
// missing fields/lyrics/better covers use the same plan as metadata-only fills.
// HISTORY_* describes the incoming audio. Caller locks the old file throughout.
// Use inspections of the exact, unchanged buffers supplied for the merge.
bool PreserveUpgradeMetadata(const std::vector<uint8_t>& existing,const RecordingFile& old,
    const std::vector<uint8_t>& incoming,const RecordingFile& next,
    const Catalog& catalog,std::vector<uint8_t>& output,std::string& error);
}
