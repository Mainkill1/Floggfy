#pragma once
#include "library_layout.h"
#include "ogg_tags.h"
#include <cstddef>
#include <cstdint>
#include <array>
namespace history {
// Inspection never decodes or changes compressed audio. It checks container
// boundaries, checksums, sample/page coverage and supported Vorbis headers.
// Header/container validation is not a full Vorbis audio-packet decode.
struct RecordingArtwork {
    bool valid=false;
    uint32_t width=0,height=0;
    size_t bytes=0;
    std::array<uint64_t,2> digest{};
    std::string mime;
};
// Structural image validation and stable fingerprints of actual encoded image
// bytes, independent of PICTURE headers or base64 representation.
bool ReadRecordingArtwork(const std::vector<uint8_t>& bytes,RecordingArtwork& out);
struct RecordingFile {
    Quality quality;
    Tags tags;
    bool complete=false;
    bool owned=false;
    bool has_artwork=false;
    bool artwork_edit_safe=true;
    RecordingArtwork artwork;
    std::vector<RecordingArtwork> artworks;
    // Exact original picture comment values, including opaque/uninspectable
    // base64, for preserving embedded artwork during compressed-audio upgrades.
    // The existing 16 MiB aggregate comment limit bounds retained values.
    std::vector<std::string> picture_comments;
};
bool InspectRecording(const uint8_t* bytes,size_t size,RecordingFile& out);
bool MatchesRecording(const RecordingFile& file,const Catalog& catalog);
std::wstring RecordingSuffix(const Catalog& catalog);
}
