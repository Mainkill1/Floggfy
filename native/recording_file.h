#pragma once
#include "library_layout.h"
#include "ogg_tags.h"
#include "recording_limits.h"
#include <cstddef>
#include <cstdint>
#include <array>
namespace history {
class CompressedBuffer;
struct FlacInfo;
// Incremental proof from compressed frames, independent of decoder callbacks.
// Scan only an append-only source with validated metadata; reset this cursor
// when replacing/compacting the source. Invalid and unfinished frames both
// leave the cursor at the last validated boundary, with complete=false.
struct FlacBufferCoverage {
    size_t next_offset=0;
    uint64_t samples=0,frames=0;
    bool complete=false;
    // Complete requires the expected samples AND exact end of input. The
    // validated prefix offset remains available when trailing bytes follow.
    void Scan(const CompressedBuffer& bytes,const FlacInfo& info,size_t audio_offset);
};
// Capture-only normalization for rereads within one append-only FLAC source.
// Reset on any external replacement/compaction. Final file validation remains
// independent and strict. Only complete, byte-proven recovery mutates bytes.
struct FlacCaptureReader {
    size_t next_offset=0;
    uint64_t samples=0,frames=0,duplicate_frames=0,duplicate_bytes=0,interrupted_bytes=0;
    // Work counters make capture stalls distinguishable from input starvation.
    uint64_t resync_bytes_examined=0,resync_candidate_parses=0;
    bool complete=false,incoming_sample_known=false;
    uint64_t incoming_sample=0;
    const char* reason="waiting_frame";
    // available is additional memory available in the shared capture budget.
    void Scan(CompressedBuffer& bytes,const FlacInfo& info,size_t audio_offset,
              size_t available=8*1024*1024);
    size_t MemoryUsage() const noexcept;
private:
    struct Frame {size_t offset,length;uint64_t sample;unsigned block;};
    struct Fragment {size_t offset,length,index;};
    std::vector<Frame> index;
    std::vector<Fragment> pending;
    std::vector<size_t> resync_pending;
    size_t resync_origin=~size_t(0),resync_next=0;
    bool recovering=false,failed=false;
};
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
// Locate a checksum-valid FLAC frame at or after begin. Both variable-block
// sample numbering and fixed-block frame numbering are validated by the same
// parser used for final file inspection.
bool FindValidatedFlacFrame(const CompressedBuffer& bytes,const Quality& quality,
                            unsigned max_block,uint64_t expected_sample,
                            uint64_t frame_number,size_t begin,size_t& offset);
struct RecordingFile {
    Quality quality;
    Tags tags;
    bool complete=false;
    bool owned=false;
    bool has_artwork=false;
    bool artwork_edit_safe=true;
    bool inspection_resource_failure=false;
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
