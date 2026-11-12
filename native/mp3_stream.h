#pragma once
#include "ogg_tags.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace history {
class CompressedBuffer;
struct Mp3Info {
    unsigned rate=0,channels=0,frames=0;
    std::uint64_t samples=0;
    std::size_t audio_offset=0,audio_size=0;
    bool complete=false;
    unsigned declared_frames=0;
    std::size_t declared_bytes=0;
    bool DeclaredComplete() const noexcept {
        return complete&&(declared_frames||declared_bytes)&&(!declared_frames||frames==declared_frames)&&
            (!declared_bytes||audio_size==declared_bytes);
    }
    double Duration() const noexcept {return rate?double(samples)/rate:0;}
};
bool InspectMp3Source(const std::uint8_t* bytes,std::size_t size,Mp3Info& out,std::string& error) noexcept;
bool InspectMp3Source(const CompressedBuffer& bytes,Mp3Info& out,std::string& error) noexcept;
// Incremental, frame-aligned source-boundary scan; cursor retains validated frames.
bool FindMp3StreamStart(const CompressedBuffer&,Mp3Info& cursor,std::size_t& offset) noexcept;
bool InspectMp3Prefix(const CompressedBuffer&,std::size_t offset,Mp3Info&) noexcept;
bool InspectMp3Range(const CompressedBuffer&,std::size_t offset,std::size_t length,Mp3Info&) noexcept;
bool MakeOwnedMp3(const std::uint8_t* source,std::size_t size,const Tags& tags,
                  std::vector<std::uint8_t>& out,std::string& error);
enum class Mp3InspectionResult { Valid,Invalid,ResourceFailure };
Mp3InspectionResult InspectOwnedMp3Result(const std::uint8_t* bytes,std::size_t size,
                                          Mp3Info& info,Tags& tags,std::string& error) noexcept;
bool InspectOwnedMp3(const std::uint8_t* bytes,std::size_t size,Mp3Info& info,
                     Tags& tags,std::string& error) noexcept;
}
