#pragma once
#include "library_layout.h"
#include "ogg_tags.h"
#include <cstddef>
#include <limits>
namespace history {
struct TagMemoryPlan {size_t output_capacity=0,incremental_peak=0;};
// No audio is copied while planning. The existing source and prior artifact
// remain charged by their owner; this bounds the replacement and writer scratch.
inline bool PlanTagConstructionMemory(Codec codec,size_t source_bytes,size_t metadata_bytes,
    size_t largest_packet,const Tags& tags,TagMemoryPlan& plan) noexcept {
    plan={};const auto maximum=std::numeric_limits<size_t>::max();
    auto add=[&](size_t& total,size_t amount){if(amount>maximum-total)return false;total+=amount;return true;};
    auto scaled=[&](size_t& total,size_t amount,size_t count){return amount<=maximum/count&&add(total,amount*count);};
    size_t fields=0;
    for(const auto& field:tags.fields)
        if(!add(fields,field.first.size())||!add(fields,field.second.size())||!add(fields,32))return false;
    size_t output=source_bytes;
    if(!scaled(output,tags.cover.size(),codec==Codec::Vorbis?2:1)||!add(output,fields)||!add(output,65536))return false;
    size_t peak=output;
    // FLAC copies metadata blocks/comments, Ogg buffers complete packets and
    // lacing in its two streams. Neither writer duplicates the audio payload.
    if(codec==Codec::Flac&&!scaled(peak,metadata_bytes,8))return false;
    if(codec==Codec::Vorbis&&!scaled(peak,largest_packet,8))return false;
    // Covers/comments coexist in input tags, serialization scratch, validation
    // and the replacement snapshot. Vector growth is bounded conservatively.
    if(!scaled(peak,tags.cover.size(),16)||!scaled(peak,fields,10)||!add(peak,1024*1024))return false;
    plan={output,peak};return true;
}
inline bool LargestOggPacket(const uint8_t* source,size_t size,size_t& largest) noexcept {
    largest=0;size_t packet=0;
    for(size_t at=0;at<size;) {
        if(size-at<27)return false;
        const size_t header=27+source[at+26];if(header>size-at)return false;
        size_t body=0;
        for(size_t i=27;i<header;++i) {
            const auto lace=source[at+i];
            if(packet>std::numeric_limits<size_t>::max()-lace)return false;
            packet+=lace;body+=lace;
            if(lace<255){if(packet>largest)largest=packet;packet=0;}
        }
        if(body>size-at-header)return false;
        at+=header+body;
    }
    return packet==0;
}
}
