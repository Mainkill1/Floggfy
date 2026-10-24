#pragma once
#include "ogg_tags.h"
#include "compressed_buffer.h"
namespace history {
enum class FlacParse { NeedMore,Valid,Invalid };
struct FlacInfo {
    unsigned rate=0,channels=0,bits=0,min_block=0,max_block=0;
    uint64_t total_samples=0;
};
FlacParse ParseFlacStreamInfo(const uint8_t* source,size_t length,FlacInfo& info);
FlacParse ParseFlacMetadata(const std::vector<uint8_t>& source,FlacInfo& info,size_t& audio);
FlacParse ParseFlacMetadata(const CompressedBuffer& source,FlacInfo& info,size_t& audio);
struct FlacCoverage {
    uint64_t samples=0; unsigned frames=0;
    bool Frame(const FlacInfo& info,unsigned block,unsigned rate,unsigned channels,unsigned bits,unsigned number_type,uint64_t number);
    bool Complete(const FlacInfo& info) const { return info.total_samples && samples==info.total_samples; }
};
// Recognize bounded, monotonic LRC timestamps without accepting plain text.
bool IsSyncedLyrics(const std::string& lyrics);
bool CopyFlacLyrics(const std::vector<uint8_t>& source,Tags& incoming);
enum class LyricsUpdate { Updated, Unchanged, Invalid };
// Read comment fields without decoding audio or requiring artwork.
bool ReadFlacComments(const std::vector<uint8_t>& source,Tags& tags,std::string& error);
// Fill missing lyrics or upgrade plain text to LRC for the same track;
// preserve all unrelated tags and every other metadata/audio block.
LyricsUpdate FillFlacLyrics(const std::vector<uint8_t>& source,const Tags& incoming,std::vector<uint8_t>& output,std::string& error);
bool TagFlac(const std::vector<uint8_t>& source,const Tags& tags,std::vector<uint8_t>& output,std::string& error);
}
