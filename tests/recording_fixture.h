#pragma once
#include <vector>
#include <cstdint>
namespace recording_fixture {
static uint16_t crc(const std::vector<uint8_t>& bytes,unsigned width,unsigned polynomial) {
    unsigned result=0;
    for(auto byte:bytes) { result^=unsigned(byte)<<(width-8); for(unsigned j=0;j<8;j++) result=((result<<1)^((result&(1u<<(width-1)))?polynomial:0))&((1u<<width)-1); }
    return uint16_t(result);
}
static std::vector<uint8_t> fixture(unsigned rate=44100,unsigned bits=16) {
    std::vector<uint8_t> bytes={'f','L','a','C',128,0,0,34}; bytes.resize(42);
    bytes[9]=16; bytes[11]=16;
    uint64_t packed=(uint64_t(rate)<<44)|(uint64_t(1)<<41)|(uint64_t(bits-1)<<36)|16;
    for(unsigned i=0;i<8;i++) bytes[18+i]=uint8_t(packed>>(56-8*i));
    std::vector<uint8_t> frame={0xff,0xf8,0x60,0x10,0,15};
    frame.push_back(uint8_t(crc(frame,8,7)));
    for(unsigned channel=0;channel<2;channel++) { frame.push_back(0); for(unsigned byte=0;byte<bits/8;byte++) frame.push_back(0); } // constant subframes
    auto checksum=crc(frame,16,0x8005); frame.push_back(uint8_t(checksum>>8)); frame.push_back(uint8_t(checksum));
    bytes.insert(bytes.end(),frame.begin(),frame.end()); return bytes;
}
static std::vector<uint8_t> page(const std::vector<uint8_t>& packet,unsigned sequence,unsigned flags,uint64_t granule=0) {
    std::vector<uint8_t> out={'O','g','g','S',0,uint8_t(flags)};
    for(unsigned i=0;i<8;i++) out.push_back(uint8_t(granule>>(8*i)));
    for(unsigned value:{42u,sequence,0u}) for(unsigned i=0;i<4;i++) out.push_back(uint8_t(value>>(8*i)));
    out.push_back(1);out.push_back(uint8_t(packet.size()));out.insert(out.end(),packet.begin(),packet.end());
    uint32_t checksum=0;
    for(auto byte:out) { checksum^=uint32_t(byte)<<24;for(unsigned i=0;i<8;i++) checksum=(checksum<<1)^((checksum&0x80000000)?0x04c11db7:0); }
    for(unsigned i=0;i<4;i++) out[22+i]=uint8_t(checksum>>(8*i));
    return out;
}
[[maybe_unused]] static std::vector<uint8_t> ogg(bool repeated_bos=false) {
    std::vector<uint8_t> id={1,'v','o','r','b','i','s'};id.resize(30);id[11]=2;id[12]=0x44;id[13]=0xac;id[28]=0xb8;id[29]=1;
    std::vector<uint8_t> comment={3,'v','o','r','b','i','s'};comment.resize(15);comment.push_back(1);
    auto out=page(id,0,2);
    for(auto part:{page(comment,1,0),page({5,'v','o','r','b','i','s'},2,0)}) out.insert(out.end(),part.begin(),part.end());
    if(repeated_bos) { auto restart=page(id,0,2);out.insert(out.end(),restart.begin(),restart.end()); }
    auto audio=page({0,1,2},repeated_bos?1:3,4,44100);out.insert(out.end(),audio.begin(),audio.end());
    return out;
}
}
