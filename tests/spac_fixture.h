#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace spac_fixture {
inline void le(std::vector<uint8_t>& out,uint64_t value,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i)out.push_back(uint8_t(value>>(8*i)));
}
// Independent, simple modulo-65535 Fletcher reference over little-endian words.
inline void checksum(std::vector<uint8_t>& out) {
    uint64_t first=65535,second=65535;
    for(size_t i=0;i<out.size();i+=2) {
        const unsigned word=out[i]+(i+1<out.size()?unsigned(out[i+1])*256:0);
        first+=word;second+=first;
        first=(first&65535)+(first>>16);second=(second&65535)+(second>>16);
    }
    first=(first&65535)+(first>>16);second=(second&65535)+(second>>16);
    le(out,(second<<16)|first,4);
}
inline std::vector<uint8_t> asc(unsigned object=2,unsigned channels=2) {
    std::vector<uint8_t> out;unsigned at=0;
    auto bits=[&](unsigned value,unsigned count) {
        for(unsigned i=count;i;--i,++at) {
            if(at%8==0)out.push_back(0);
            out.back()|=uint8_t(((value>>(i-1))&1)<<(7-at%8));
        }
    };
    if(object>=32){bits(31,5);bits(object-32,6);}else bits(object,5);
    bits(object==2?4:7,4);bits(channels,4);
    if(object==5||object==29){bits(4,4);bits(2,5);}
    bits(0,3);return out;
}
inline std::vector<uint8_t> config(unsigned object=2,unsigned total=2500,unsigned priming=100) {
    const auto extra=asc(object,object==29?1:2);
    std::vector<uint8_t> out={1,0,'m','p','4','a'};
    le(out,total,4);out.push_back(2);le(out,44100,3);le(out,priming,2);
    le(out,object==2?1024:2048,2);le(out,extra.size(),2);
    out.insert(out.end(),extra.begin(),extra.end());return out;
}
inline std::vector<uint8_t> page(bool setup,uint32_t base,const std::vector<std::vector<uint8_t>>& packets,bool shared=false) {
    std::vector<uint8_t> laces,body;
    const uint8_t prefix=shared?packets.front().front():0;
    for(const auto& packet:packets) {
        const size_t skip=shared?1:0;size_t length=packet.size()-skip;
        while(length>=255){laces.push_back(255);length-=255;}
        laces.push_back(uint8_t(length));body.insert(body.end(),packet.begin()+skip,packet.end());
    }
    std::vector<uint8_t> out={'S','p','A','C',uint8_t((setup?128:0)|(shared?1:0)),uint8_t(laces.size())};
    if(!setup) {
        const uint32_t value=base+1;
        if(value<0x8000){out.push_back(uint8_t(value>>8));out.push_back(uint8_t(value));}
        else if(value<0x400000){out.push_back(uint8_t(0x80|(value>>16)));out.push_back(uint8_t(value>>8));out.push_back(uint8_t(value));}
        else {out.push_back(0xc0);out.push_back(uint8_t(value>>16));out.push_back(uint8_t(value>>8));out.push_back(uint8_t(value));}
    }
    out.insert(out.end(),laces.begin(),laces.end());if(shared)out.push_back(prefix);
    out.insert(out.end(),body.begin(),body.end());checksum(out);return out;
}
inline void append(std::vector<uint8_t>& target,const std::vector<uint8_t>& bytes){target.insert(target.end(),bytes.begin(),bytes.end());}
inline std::vector<uint8_t> stream(unsigned object=2,bool shared=false) {
    auto out=page(true,0,{config(object)});
    const std::vector<uint8_t> packet={0x21,0x10,0x04};
    append(out,page(false,0,object==2?std::vector<std::vector<uint8_t>>{packet,packet,packet}:std::vector<std::vector<uint8_t>>{packet,packet},shared));return out;
}
}
