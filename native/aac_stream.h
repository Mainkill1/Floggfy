#pragma once
#include "ogg_tags.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace history {
// SpAC timing describes rendered samples, including HE-AAC rate expansion.
// Preserve the exact decoder configuration; never synthesize ADTS headers.
struct AacInfo {
    uint32_t rate=0,channels=0,samples_per_packet=0,priming=0,total_samples=0;
    std::vector<uint8_t> config;
};
enum class AacParse { NeedMore,Complete,Invalid,Unsupported };
class SpacStream {
    std::vector<uint8_t> pending_;
    uint64_t frames_=0;
    bool configured_=false,complete_=false,failed_=false;
    AacParse ParsePage(const uint8_t* page,size_t size,
                       std::vector<std::vector<uint8_t>>* packets);
public:
    AacInfo info;
    // Holds at most one bounded partial page; no file or decoder operations.
    AacParse Push(const uint8_t* bytes,size_t size);
    double Duration() const {return info.rate?double(info.total_samples)/info.rate:0;}
    bool Complete() const {return complete_&&!failed_;}
    friend bool ReadSpac(const std::vector<uint8_t>&,AacInfo&,
                         std::vector<std::vector<uint8_t>>&,std::string&);
};
// Publication-only extraction, bounded to a complete supported SpAC stream.
bool ReadSpac(const std::vector<uint8_t>& source,AacInfo& info,
              std::vector<std::vector<uint8_t>>& packets,std::string& error);
struct AacFile {
    AacInfo info;
    Tags tags;
    uint64_t audio_bytes=0;
    bool complete=false;
};
// Remux original access units into M4A, including known source priming/trimming.
bool MakeAacM4a(const AacInfo&,const std::vector<std::vector<uint8_t>>& packets,
               const Tags&,std::vector<uint8_t>& output,std::string& error);
// Strict bounded container/header inspection, not an independent AAC decode.
bool InspectAacM4a(const uint8_t* source,size_t size,AacFile& out);
// Preserve compressed media and unknown metadata; apply only supplied tag deltas.
bool TagAacM4a(const std::vector<uint8_t>& source,const Tags& tags,
              std::vector<uint8_t>& output,std::string& error);
}
