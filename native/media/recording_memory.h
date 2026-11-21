#pragma once
#include "library_layout.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
namespace history {
using RecordingMemoryPermit=bool(*)(void*,size_t incremental_peak);
// Nested scopes include the buffers retained by each enclosing operation.
// Denial remains visible through all enclosing scopes.
class RecordingReadMemoryScope {
    inline static thread_local RecordingReadMemoryScope* current_=nullptr;
    RecordingReadMemoryScope* previous_=nullptr;
    RecordingMemoryPermit permit_=nullptr;void* context_=nullptr;
    size_t retained_=0;bool denied_=false;
    void Deny() noexcept {for(auto* scope=this;scope;scope=scope->previous_)scope->denied_=true;}
public:
    explicit RecordingReadMemoryScope(RecordingMemoryPermit permit=nullptr,void* context=nullptr) noexcept
        :previous_(current_),permit_(permit),context_(context){current_=this;}
    ~RecordingReadMemoryScope(){current_=previous_;}
    RecordingReadMemoryScope(const RecordingReadMemoryScope&)=delete;
    RecordingReadMemoryScope& operator=(const RecordingReadMemoryScope&)=delete;
    bool Request(size_t peak) noexcept {
        if(peak>std::numeric_limits<size_t>::max()-retained_){Deny();return false;}
        const auto total=retained_+peak;bool allowed=false;
        try {allowed=permit_?permit_(context_,total):!previous_||previous_->Request(total);}
        catch(...) {allowed=false;}
        if(!allowed)Deny();
        return allowed;
    }
    void SetRetainedBytes(size_t bytes) noexcept {retained_=bytes;}
    bool Denied() const noexcept {return denied_;}
    static bool AdmitCurrent(size_t bytes) noexcept {return !current_||current_->Request(bytes);}
    static bool HasPermit() noexcept {
        for(auto* scope=current_;scope;scope=scope->previous_)if(scope->permit_)return true;
        return false;
    }
};
inline constexpr size_t kVorbisArenaLimit=8*1024*1024;
struct RecordingMemoryPlan {size_t file_bytes=0,metadata_bytes=0,inspection_peak=0;};
inline bool EstimateRecordingReadMemory(size_t bytes,size_t metadata,size_t& peak,bool copy_payload=false) noexcept {
    peak=0;const auto maximum=std::numeric_limits<size_t>::max();
    constexpr size_t arena=kVorbisArenaLimit;
    if(metadata>bytes||bytes>maximum-arena||metadata>(maximum-bytes-arena)/32)return false;
    size_t total=bytes+arena+metadata*32;
    if(copy_payload){if(bytes>maximum-total)return false;total+=bytes;}
    peak=total;return true;
}
// Reads only container headers/lacing, never audio or metadata bodies. Reader
// must copy exactly n bytes at offset and accept no range outside the file.
template<class Reader> bool ProbeRecordingMemory(size_t size,const Reader& read,RecordingMemoryPlan& plan) {
    plan={};if(size<4)return false;
    uint8_t header[282]{};
    const auto first=std::min(size,size_t{16});if(!read(0,header,first))return false;
    size_t metadata=0;bool copy_payload=false;
    if(!std::memcmp(header,"fLaC",4)) {
        size_t at=4;
        for(;;) {
            if(at>size||size-at<4||!read(at,header,4))return false;
            const size_t length=(size_t(header[1])<<16)|(size_t(header[2])<<8)|header[3];
            if(length>size-at-4)return false;
            at+=4+length;metadata=at;
            if(header[0]&128)break;
        }
    } else if(!std::memcmp(header,"OggS",4)) {
        size_t at=0;unsigned packets=0;
        while(packets<3) {
            if(at>size||size-at<27||!read(at,header,27)||std::memcmp(header,"OggS",4))return false;
            const size_t laces=header[26];if(laces>size-at-27||!read(at+27,header+27,laces))return false;
            size_t body=0;
            for(size_t i=0;i<laces;++i) {
                const auto length=header[27+i];body+=length;
                if(packets<3){if(length>size-metadata)return false;metadata+=length;if(length<255)++packets;}
            }
            if(body>size-at-27-laces)return false;
            at+=27+laces+body;
        }
    } else if(!std::memcmp(header,"ID3",3)) {
        if(size<10||(header[6]|header[7]|header[8]|header[9])&128)return false;
        metadata=10+(size_t(header[6])<<21)+(size_t(header[7])<<14)+(size_t(header[8])<<7)+header[9];
        if(metadata>size)return false;
    } else if(size>=12&&!std::memcmp(header+4,"ftyp",4)) {
        copy_payload=true;
        size_t at=0;
        while(at<size) {
            if(size-at<8||!read(at,header,8))return false;
            uint64_t length=0;for(unsigned i=0;i<4;++i)length=(length<<8)|header[i];
            size_t box_header=8;
            if(length==1) {
                if(size-at<16||!read(at+8,header+8,8))return false;
                length=0;for(unsigned i=8;i<16;++i)length=(length<<8)|header[i];box_header=16;
            }
            if(length<box_header||length>size-at)return false;
            if(!std::memcmp(header+4,"moov",4)){if(length>size-metadata)return false;metadata+=size_t(length);}
            at+=size_t(length);
        }
    }
    size_t peak=0;if(!EstimateRecordingReadMemory(size,metadata,peak,copy_payload))return false;
    plan={size,metadata,peak};return true;
}
inline bool ProbeRecordingMemory(const uint8_t* bytes,size_t size,RecordingMemoryPlan& plan) {
    if(!bytes){plan={};return false;}
    return ProbeRecordingMemory(size,[&](size_t at,uint8_t* out,size_t n){
        if(at>size||n>size-at)return false;
        std::memcpy(out,bytes+at,n);return true;
    },plan);
}
// Additional transaction memory excludes the caller-owned incoming artifact.
// Includes held old bytes, replacement copy, prepared FLAC/preserved outputs,
// vector growth, metadata objects/sets/artwork and bounded parser scratch.
inline bool EstimatePublicationUpgradeMemory(Codec codec,size_t existing,size_t incoming,
    size_t existing_metadata,size_t incoming_metadata,size_t& peak) noexcept {
    peak=0;if(existing_metadata>existing||incoming_metadata>incoming)return false;
    const auto maximum=std::numeric_limits<size_t>::max();size_t total=8*1024*1024;
    auto add=[&](size_t count,size_t each=1){if(count>(maximum-total)/each)return false;total+=count*each;return true;};
    if(!add(existing)||!add(incoming,codec==Codec::Vorbis?12:4)||
       !add(existing_metadata,40)||!add(incoming_metadata,40))return false;
    peak=total;return true;
}
}
