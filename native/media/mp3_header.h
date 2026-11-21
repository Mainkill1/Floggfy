#pragma once
#include <cstddef>
#include <cstdint>

namespace history {
inline bool ReadId3Syncsafe(const std::uint8_t* bytes,std::size_t& value) noexcept {
    if((bytes[0]|bytes[1]|bytes[2]|bytes[3])&0x80)return false;
    value=(std::size_t(bytes[0])<<21)|(std::size_t(bytes[1])<<14)|
          (std::size_t(bytes[2])<<7)|bytes[3];
    return true;
}

struct Mp3FrameHeader {
    std::uint32_t bits;
    unsigned Version() const noexcept {return (bits>>19)&3;}
    unsigned BitrateIndex() const noexcept {return (bits>>12)&15;}
    unsigned RateIndex() const noexcept {return (bits>>10)&3;}
    const char* Error() const noexcept {
        if((bits&0xffe00000u)!=0xffe00000u)return "invalid MP3 frame sync";
        if(Version()==1)return "reserved MPEG version";
        if(((bits>>17)&3)!=1)return "unsupported MPEG layer";
        if(!BitrateIndex()||BitrateIndex()==15)return "unsupported MP3 bitrate";
        if(RateIndex()==3)return "reserved MP3 sample rate";
        return nullptr;
    }
    unsigned Channels() const noexcept {return ((bits>>6)&3)==3?1:2;}
    // Use the rate and frame length only after Error() accepts the header.
    unsigned Rate() const noexcept {
        constexpr unsigned rates[]={44100,48000,32000};
        const auto version=Version();
        return rates[RateIndex()]/(version==3?1:version==2?2:4);
    }
    unsigned FrameBytes() const noexcept {
        constexpr unsigned mpeg1[]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320};
        constexpr unsigned mpeg2[]={0,8,16,24,32,40,48,56,64,80,96,112,128,144,160};
        const auto bitrate=(Version()==3?144000*mpeg1[BitrateIndex()]:72000*mpeg2[BitrateIndex()]);
        return bitrate/Rate()+((bits>>9)&1);
    }
};
}
