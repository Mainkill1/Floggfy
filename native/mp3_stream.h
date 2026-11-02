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
    double Duration() const noexcept {return rate?double(samples)/rate:0;}
};
bool InspectMp3Source(const std::uint8_t* bytes,std::size_t size,Mp3Info& out,std::string& error) noexcept;
bool InspectMp3Source(const CompressedBuffer& bytes,Mp3Info& out,std::string& error) noexcept;
bool MakeOwnedMp3(const std::uint8_t* source,std::size_t size,const Tags& tags,
                  std::vector<std::uint8_t>& out,std::string& error);
bool InspectOwnedMp3(const std::uint8_t* bytes,std::size_t size,Mp3Info& info,
                     Tags& tags,std::string& error) noexcept;
}
