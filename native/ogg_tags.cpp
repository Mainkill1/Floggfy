#include "ogg_tags.h"
#include "ogg_history_core.h"
#include <ogg/ogg.h>
#include <algorithm>
#include <cstring>
#include <limits>
namespace history {
static void Number(std::vector<uint8_t>& v,uint32_t n,bool big=false) {
    for(int i=0;i<4;i++) v.push_back(uint8_t(n>>(8*(big ? 3-i : i))));
}
static uint32_t Little(const uint8_t* p) {
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
static std::string Upper(std::string s) {
    for(char& c:s) if(c>='a' && c<='z') c=char(c-'a'+'A');
    return s;
}
static std::string Base64(const std::vector<uint8_t>& v) {
    const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; out.reserve(((v.size()+2)/3)*4);
    for(size_t i=0;i<v.size();i+=3) {
        uint32_t n=uint32_t(v[i])<<16;
        if(i+1<v.size()) n|=uint32_t(v[i+1])<<8;
        if(i+2<v.size()) n|=v[i+2];
        out+=alphabet[n>>18]; out+=alphabet[(n>>12)&63];
        out+=i+1<v.size() ? alphabet[(n>>6)&63] : '=';
        out+=i+2<v.size() ? alphabet[n&63] : '=';
    } return out;
}
bool BuildPicture(const Tags& tags,std::vector<uint8_t>& picture) {
    picture.clear();
    if(tags.cover.empty() || tags.cover.size()>4*1024*1024 ||
       (tags.mime!="image/png" && tags.mime!="image/jpeg")) return false;
    Number(picture,3,true);
    Number(picture,uint32_t(tags.mime.size()),true); picture.insert(picture.end(),tags.mime.begin(),tags.mime.end());
    Number(picture,0,true);
    for(int i=0;i<4;i++) Number(picture,0,true);
    Number(picture,uint32_t(tags.cover.size()),true); picture.insert(picture.end(),tags.cover.begin(),tags.cover.end());
    return true;
}
static bool Comments(const ogg_packet& packet,const Tags& tags,std::vector<uint8_t>& out) {
    if(packet.bytes<16 || std::memcmp(packet.packet,"\x03vorbis",7)) return false;
    size_t offset=7,limit=size_t(packet.bytes);
    auto read=[&](std::string& s) {
        if(offset+4>limit) return false;
        uint32_t n=Little(packet.packet+offset); offset+=4;
        if(n>limit-offset) return false;
        s.assign(reinterpret_cast<char*>(packet.packet+offset),n); offset+=n; return true;
    };
    std::string vendor;
    if(!read(vendor) || offset+4>limit) return false;
    uint32_t count=Little(packet.packet+offset); offset+=4;
    if(count>100000 || count>(limit-offset)/4) return false;
    std::vector<std::string> fields;
    for(uint32_t i=0;i<count;i++) {
        std::string field; if(!read(field)) return false;
        std::string key=Upper(field.substr(0,field.find('=')));
        bool replace=key=="METADATA_BLOCK_PICTURE";
        for(const auto& item:tags.fields) if(key==Upper(item.first)) replace=true;
        if(!replace) fields.push_back(std::move(field));
    }
    if(offset+1!=limit || packet.packet[offset]!=1) return false;
    for(const auto& item:tags.fields) fields.push_back(item.first+'='+item.second);
    std::vector<uint8_t> picture;
    if(!BuildPicture(tags,picture)) return false;
    fields.push_back("METADATA_BLOCK_PICTURE="+Base64(picture));
    out.assign({'\x03','v','o','r','b','i','s'});
    if(vendor.empty()) vendor="SpotifyRepair native history";
    Number(out,uint32_t(vendor.size())); out.insert(out.end(),vendor.begin(),vendor.end());
    Number(out,uint32_t(fields.size()));
    for(const auto& field:fields) {
        Number(out,uint32_t(field.size())); out.insert(out.end(),field.begin(),field.end());
    }
    out.push_back(1); return out.size()<=16*1024*1024;
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
            output.reserve(source.size()+tags.cover.size()*4/3+65536);
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
