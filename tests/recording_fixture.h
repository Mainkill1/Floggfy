#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>
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
static std::vector<uint8_t> page_parts(const std::vector<uint8_t>& body,const std::vector<uint8_t>& lacing,
                                     unsigned sequence,unsigned flags,uint64_t granule=0,uint32_t serial=42) {
    std::vector<uint8_t> out={'O','g','g','S',0,uint8_t(flags)};
    for(unsigned i=0;i<8;i++) out.push_back(uint8_t(granule>>(8*i)));
    for(unsigned value:{serial,sequence,0u}) for(unsigned i=0;i<4;i++) out.push_back(uint8_t(value>>(8*i)));
    out.push_back(uint8_t(lacing.size()));out.insert(out.end(),lacing.begin(),lacing.end());
    out.insert(out.end(),body.begin(),body.end());
    uint32_t checksum=0;
    for(auto byte:out) { checksum^=uint32_t(byte)<<24;for(unsigned i=0;i<8;i++) checksum=(checksum<<1)^((checksum&0x80000000)?0x04c11db7:0); }
    for(unsigned i=0;i<4;i++) out[22+i]=uint8_t(checksum>>(8*i));
    return out;
}
static std::vector<uint8_t> page(const std::vector<uint8_t>& packet,unsigned sequence,unsigned flags,uint64_t granule=0,uint32_t serial=42) {
    std::vector<uint8_t> lacing(packet.size()/255,255);lacing.push_back(uint8_t(packet.size()%255));
    return page_parts(packet,lacing,sequence,flags,granule,serial);
}
static std::vector<uint8_t> identification() {
    std::vector<uint8_t> id={1,'v','o','r','b','i','s'};id.resize(30);id[11]=2;id[12]=0x44;id[13]=0xac;
    id[28]=0x66;id[29]=1; // Two 64-sample blocks; Vorbis I identification framing.
    return id;
}
static std::vector<uint8_t> setup(unsigned entries=1,unsigned codeword_length=1) {
    // A complete minimal Vorbis I setup, not a seven-byte setup signature:
    // one singleton scalar codebook, floor 1 with no partitions, empty residue,
    // one uncoupled mapping, one short-block mode, and the final framing bit.
    std::vector<uint8_t> packet={5,'v','o','r','b','i','s'};size_t position=56;
    auto bits=[&](uint32_t value,unsigned count) {
        for(unsigned i=0;i<count;++i,++position) {
            if(position/8==packet.size())packet.push_back(0);
            packet[position/8]|=uint8_t(((value>>i)&1)<<(position%8));
        }
    };
    bits(0,8);bits(0x564342,24);bits(1,16);bits(entries,24); // One one-dimensional codebook.
    bits(0,1);bits(0,1); // Unordered, dense codeword list.
    for(unsigned i=0;i<entries;++i)bits(codeword_length-1,5);
    bits(0,4); // Scalar lookup type 0.
    bits(0,6);bits(0,16); // One unused time transform.
    bits(0,6);bits(1,16);bits(0,5);bits(0,2);bits(1,4); // Floor 1, endpoints 0 and 2.
    bits(0,6);bits(0,16);bits(0,24);bits(0,24);bits(0,24); // Empty type-0 residue.
    bits(0,6);bits(0,8);bits(0,3);bits(0,1); // One class, scalar classbook, no pass books.
    bits(0,6);bits(0,16);bits(0,1);bits(0,1);bits(0,2); // One mapping, no submaps/coupling.
    bits(0,8);bits(0,8);bits(0,8); // Time/floor/residue selectors.
    bits(0,6);bits(0,1);bits(0,16);bits(0,16);bits(0,8);bits(1,1); // One mode and framing.
    return packet;
}
static std::vector<uint8_t> ogg_headers(const std::vector<uint8_t>& id,const std::vector<uint8_t>& configuration,
                                        bool repeated_bos=false,const std::vector<uint8_t>& supplied_comment={}) {
    std::vector<uint8_t> comment={3,'v','o','r','b','i','s'};comment.resize(15);comment.push_back(1);
    if(!supplied_comment.empty())comment=supplied_comment;
    auto out=page(id,0,2);
    for(auto part:{page(comment,1,0),page(configuration,2,0)}) out.insert(out.end(),part.begin(),part.end());
    if(repeated_bos) { auto restart=page(id,0,2);out.insert(out.end(),restart.begin(),restart.end()); }
    // Each zero packet selects the short mode and absent floors (silence).
    // The first packet primes the overlap. 1,379 subsequent packets cover
    // 44,128 samples, with EOS trimming the final block to one 44,100-Hz second.
    unsigned sequence=repeated_bos?1:3,packets=0;
    while(packets<1380) {
        const auto count=1380-packets>255?255:1380-packets;packets+=count;
        auto audio=page_parts(std::vector<uint8_t>(count,0),std::vector<uint8_t>(count,1),sequence++,
                              packets==1380?4:0,packets==1380?44100:32*(packets-1));
        out.insert(out.end(),audio.begin(),audio.end());
    }
    return out;
}
[[maybe_unused]] static std::vector<uint8_t> ogg(bool repeated_bos=false) {
    return ogg_headers(identification(),setup(),repeated_bos);
}
}
