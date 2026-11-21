#pragma once
#include "../tooling/binary.h"
#include "ogg_tags.h"
#include <cstring>
#include <set>
#include <string_view>

namespace history {
inline std::string CommentKey(std::string_view text) {
    std::string key(text);
    for(char& c:key) if(c>='a' && c<='z') c=char(c-'a'+'A');
    return key;
}

// Walk the shared FLAC/Vorbis layout without loading the vendor or fields.
template<class Reader,class Visitor>
bool ReadVorbisCommentRanges(uint64_t size,const Reader& get,Visitor visit,bool framed=false) {
    uint64_t at=0;
    const auto length=[&](uint32_t& value) {
        uint8_t bytes[4];
        if(size-at<sizeof(bytes) || !get(at,bytes,sizeof(bytes)))return false;
        at+=sizeof(bytes);
        value=binary::ReadLittle32(bytes);
        return true;
    };
    const auto field=[&](bool vendor) {
        uint32_t bytes=0;
        if(!length(bytes) || bytes>size-at || !visit(at,bytes,vendor))return false;
        at+=bytes;
        return true;
    };
    uint32_t count=0;
    if(!field(true) || !length(count) || count>100000 || count>(size-at)/4)return false;
    for(uint32_t i=0;i<count;++i)
        if(!field(false))return false;
    uint8_t framing=0;
    return framed?size-at==1 && get(at,&framing,1) && framing==1:at==size;
}

// The contiguous reader lets its visitor borrow each field from the source.
template<class Visitor>
bool ReadVorbisComments(const uint8_t* bytes,size_t size,std::string_view& vendor,Visitor visit) {
    const auto get=[&](uint64_t at,void* out,size_t length) {
        std::memcpy(out,bytes+at,length);
        return true;
    };
    return ReadVorbisCommentRanges(size,get,[&](uint64_t at,uint32_t length,bool is_vendor) {
        const std::string_view field(reinterpret_cast<const char*>(bytes+at),length);
        if(is_vendor)vendor=field;
        else visit(field);
        return true;
    });
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
