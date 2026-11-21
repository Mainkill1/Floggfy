#include "../tooling/base64.h"
#include "vorbis_comments.h"
#include "tag_memory_plan.h"
#include "image_info.h"
#include "ogg_history_core.h"
#include <ogg/ogg.h>
#include <algorithm>
#include <cstring>
#include <limits>
namespace history {
static void WriteBig32(std::vector<uint8_t>& v,uint32_t n) {
    for(int i=0;i<4;i++) v.push_back(uint8_t(n>>(8*(3-i))));
}
bool BuildPicture(const Tags& tags,std::vector<uint8_t>& picture) {
    picture.clear();
    ImageInfo info;
    if(!ReadImageInfo(tags.cover,info) || tags.mime!=info.mime) return false;
    WriteBig32(picture,3);
    WriteBig32(picture,uint32_t(tags.mime.size())); picture.insert(picture.end(),tags.mime.begin(),tags.mime.end());
    WriteBig32(picture,0);
    WriteBig32(picture,info.width); WriteBig32(picture,info.height);
    WriteBig32(picture,info.depth); WriteBig32(picture,info.colors);
    WriteBig32(picture,uint32_t(tags.cover.size())); picture.insert(picture.end(),tags.cover.begin(),tags.cover.end());
    return true;
}
static bool Comments(const ogg_packet& packet,const Tags& tags,std::vector<uint8_t>& out) {
    if(packet.bytes<16 || std::memcmp(packet.packet,"\x03vorbis",7) || packet.packet[packet.bytes-1]!=1)return false;
    std::vector<uint8_t> picture;
    const bool has_picture=BuildPicture(tags,picture);
    VorbisComments comments;
    if(!comments.Read(packet.packet+7,size_t(packet.bytes)-8,tags,has_picture))return false;
    if(comments.vendor.empty())comments.vendor="SpotifyRepair native history";
    out.assign({'\x03','v','o','r','b','i','s'});
    std::string extra;
    if(has_picture) {
        extra="METADATA_BLOCK_PICTURE=";
        extra.reserve(extra.size()+base64::EncodedSize(picture.size()));
        base64::Append(extra,{reinterpret_cast<const char*>(picture.data()),picture.size()});
    }
    if(!comments.Write(out,tags,16*1024*1024-1,extra))return false;
    out.push_back(1);return true;
}
struct OggStreams {
    ogg_stream_state input={},output={}; bool initialized=false;
    ~OggStreams() { if(initialized) { ogg_stream_clear(&input); ogg_stream_clear(&output); } }
};
bool TagOgg(const std::vector<uint8_t>& source,const Tags& tags,
            std::vector<uint8_t>& output,std::string& error) {
    output.clear(); error.clear();
    auto fail=[&](const char* why) { error=why; output.clear(); return false; };
    if(source.empty()) return fail("empty Ogg source");
    OggStreams streams; Stream validation;
    size_t offset=0; int64_t packet_number=0;
    std::vector<uint8_t> comments;
    while(offset<source.size()) {
        if(source.size()-offset<27) return fail("truncated Ogg header");
        const uint8_t* b=source.data()+offset; size_t header=27+b[26];
        if(header>source.size()-offset) return fail("truncated Ogg lacing");
        size_t body=0; for(size_t i=27;i<header;i++) body+=b[i];
        size_t size=header+body;
        if(size>source.size()-offset) return fail("truncated Ogg body");
        if(validation.complete) return fail("trailing stream after EOS");
        Result state=validation.Push(b,size);
        if(state==Result::Invalid || state==Result::Ignore) return fail("invalid Ogg stream or sequence gap");
        if(!streams.initialized) {
            if(ogg_stream_init(&streams.input,int(validation.serial))) return fail("input Ogg allocation failed");
            if(ogg_stream_init(&streams.output,int(validation.serial))) {
                ogg_stream_clear(&streams.input); return fail("output Ogg allocation failed");
            }
            streams.initialized=true;
            TagMemoryPlan plan;if(!PlanTagConstructionMemory(Codec::Vorbis,source.size(),0,0,tags,plan))return fail("Ogg tag allocation overflow");
            output.reserve(plan.output_capacity);
        }
        ogg_page page={const_cast<unsigned char*>(b),long(header),const_cast<unsigned char*>(b+header),long(body)};
        if(ogg_stream_pagein(&streams.input,&page)) return fail("Ogg page rejected");
        ogg_packet packet; int result;
        auto flush=[&] {
            ogg_page encoded;
            while(ogg_stream_flush(&streams.output,&encoded)) {
                output.insert(output.end(),encoded.header,encoded.header+encoded.header_len);
                output.insert(output.end(),encoded.body,encoded.body+encoded.body_len);
            }
        };
        while((result=ogg_stream_packetout(&streams.input,&packet))!=0) {
            if(result<0) return fail("Ogg packet gap");
            if(packet_number==0 && (packet.bytes<30 || std::memcmp(packet.packet,"\x01vorbis",7)))
                return fail("missing Vorbis identification");
            if(packet_number==1) {
                if(!Comments(packet,tags,comments)) return fail("invalid Vorbis comments or artwork");
                packet.packet=comments.data(); packet.bytes=long(comments.size());
            }
            if(packet_number==2 && (packet.bytes<7 || std::memcmp(packet.packet,"\x05vorbis",7)))
                return fail("missing Vorbis setup");
            if(packet_number<3) packet.granulepos=0;
            if(ogg_stream_packetin(&streams.output,&packet)) return fail("Ogg packet output failed");
            if(packet_number<3) flush();
            ++packet_number;
        }
        flush(); offset+=size;
    }
    if(!validation.complete || packet_number<4 || !ogg_stream_eos(&streams.output))
        return fail("incomplete Ogg EOS");
    return true;
}
}
