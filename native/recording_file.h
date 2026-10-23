#pragma once
#include "library_layout.h"
#include "ogg_tags.h"
#include <cstddef>
#include <cstdint>
namespace history {
// Inspection never decodes or changes compressed audio. It checks container
// boundaries, checksums and complete sample/page coverage before trusting quality.
struct RecordingFile {
    Quality quality;
    Tags tags;
    bool complete=false;
    bool owned=false;
};
bool InspectRecording(const uint8_t* bytes,size_t size,RecordingFile& out);
bool MatchesRecording(const RecordingFile& file,const Catalog& catalog);
std::wstring RecordingSuffix(const Catalog& catalog);
}
