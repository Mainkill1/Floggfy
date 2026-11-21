#pragma once
#include "ogg_tags.h"
#include <set>
#include <string_view>

namespace history {

inline uint32_t ReadLittle32(const uint8_t* p) {
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}

inline std::string CommentKey(std::string_view text) {
    std::string key(text);
    for(char& c:key) if(c>='a' && c<='z') c=char(c-'a'+'A');
    return key;
}

// FLAC and Vorbis share this body. Ogg's packet signature and framing bit
// are checked by its caller. The visitor borrows each field from the source.
template<class Visitor>
bool ReadVorbisComments(const uint8_t* bytes,size_t size,std::string_view& vendor,Visitor visit) {
    size_t at=0;
    auto read=[&](std::string_view& text) {
        if(size-at<4)return false;
        const auto length=ReadLittle32(bytes+at);at+=4;
        if(length>size-at)return false;
        text={reinterpret_cast<const char*>(bytes+at),length};at+=length;
        return true;
    };
    if(!read(vendor) || size-at<4)return false;
    const auto count=ReadLittle32(bytes+at);at+=4;
    if(count>100000 || count>(size-at)/4)return false;
    for(uint32_t i=0;i<count;++i) {
        std::string_view field;
        if(!read(field))return false;
        visit(field);
    }
    return at==size;
}

struct VorbisComments {
    std::string_view vendor="SpotifyRepair native history";
    std::vector<std::string_view> kept;

    bool Read(const uint8_t* bytes,size_t size,const Tags& tags,bool drop_picture,bool keep_unkeyed=false) {
        std::set<std::string> replaced;
        for(const auto& tag:tags.fields)replaced.insert(CommentKey(tag.first));
        if(drop_picture)replaced.insert("METADATA_BLOCK_PICTURE");
        return ReadVorbisComments(bytes,size,vendor,[&](std::string_view field) {
            if(tags.replace_fields)return;
            const auto split=field.find('=');
            if((keep_unkeyed && split==field.npos) || !replaced.count(CommentKey(field.substr(0,split))))
                kept.push_back(field);
        });
    }

    bool Write(std::vector<uint8_t>& out,const Tags& tags,size_t limit,std::string_view extra={}) const {
        auto number=[&](size_t n){for(unsigned i=0;i<4;i++)out.push_back(uint8_t(n>>(8*i)));};
        auto append=[&](std::string_view field) {
            if(out.size()>limit || limit-out.size()<4 || field.size()>limit-out.size()-4)return false;
            number(field.size());out.insert(out.end(),field.begin(),field.end());return true;
        };
        if(!append(vendor) || limit-out.size()<4)return false;
        number(kept.size()+tags.fields.size()+!extra.empty());
        for(auto field:kept)if(!append(field))return false;
        for(const auto& tag:tags.fields)if(!append(tag.first+'='+tag.second))return false;
        return extra.empty() || append(extra);
    }
};

}
