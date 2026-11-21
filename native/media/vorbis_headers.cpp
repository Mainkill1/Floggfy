#include "../tooling/binary.h"
#include "recording_memory.h"
#include "vorbis_headers.h"
#include <ogg/ogg.h>
#include <cstring>
#include <mutex>
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_PULLDATA_API
#include "../vendor/stb/stb_vorbis.c"

namespace history {
namespace {
constexpr size_t setup_limit=1024*1024,initial_arena=64*1024;
struct StreamState {
    ogg_stream_state value{};
    bool initialized=false;
    ~StreamState(){if(initialized)ogg_stream_clear(&value);}
};
bool HeaderPages(const std::vector<uint8_t>& identification,const std::vector<uint8_t>& setup,
                 std::vector<uint8_t>& pages) {
    StreamState stream;
    if(ogg_stream_init(&stream.value,1)!=0)return false;
    stream.initialized=true;
    auto packet=[&](const uint8_t* bytes,size_t size,long number) {
        ogg_packet input{};
        input.packet=const_cast<unsigned char*>(bytes);input.bytes=long(size);
        input.b_o_s=number==0;input.packetno=number;
        if(ogg_stream_packetin(&stream.value,&input)!=0)return false;
        ogg_page page{};
        while(ogg_stream_flush(&stream.value,&page)!=0) {
            if(page.header_len<0 || page.body_len<0 ||
               size_t(page.header_len)+size_t(page.body_len)>setup_limit+65536-pages.size())return false;
            pages.insert(pages.end(),page.header,page.header+page.header_len);
            pages.insert(pages.end(),page.body,page.body+page.body_len);
        }
        return true;
    };
    // Original comments were already bounded and validated by InspectRecording.
    // They do not affect setup semantics; avoid a second potentially large
    // artwork/comment allocation inside the upstream header parser.
    const uint8_t comments[]={3,'v','o','r','b','i','s',0,0,0,0,0,0,0,0,1};
    return packet(identification.data(),identification.size(),0) &&
           packet(comments,sizeof(comments),1) && packet(setup.data(),setup.size(),2);
}
}
VorbisHeadersResult ValidateVorbisHeaders(const std::vector<uint8_t>& identification,
                                         const std::vector<uint8_t>& setup) noexcept {
    if(setup.size()>setup_limit)return VorbisHeadersResult::Unsupported;
    if(identification.size()!=30 || identification[0]!=1 ||
       std::memcmp(identification.data()+1,"vorbis",6) || setup.size()<7 || setup[0]!=5 ||
       std::memcmp(setup.data()+1,"vorbis",6))return VorbisHeadersResult::Invalid;
    if(identification[11]>8)return VorbisHeadersResult::Unsupported;
    try {
        // Allow for libogg's packet storage and growth of the repaged headers.
        if(!RecordingReadMemoryScope::AdmitCurrent(setup.size()*8+64*1024))return VorbisHeadersResult::ResourceFailure;
        std::vector<uint8_t> pages;
        if(!HeaderPages(identification,setup,pages))return VorbisHeadersResult::ResourceFailure;
        // Upstream initializes a shared CRC table during header opening.
        static std::mutex parser_lock;
        for(size_t bytes=initial_arena;bytes<=kVorbisArenaLimit;bytes*=2) {
            if(!RecordingReadMemoryScope::AdmitCurrent(pages.capacity()+bytes))return VorbisHeadersResult::ResourceFailure;
            std::vector<uint64_t> arena(bytes/sizeof(uint64_t));
            stb_vorbis_alloc allocation{reinterpret_cast<char*>(arena.data()),int(bytes)};
            int consumed=0,error=0;
            std::lock_guard<std::mutex> guard(parser_lock);
            auto* decoder=stb_vorbis_open_pushdata(pages.data(),int(pages.size()),&consumed,&error,&allocation);
            if(!decoder) {
                if(error==VORBIS_outofmem)continue;
                return error==VORBIS_feature_not_supported || error==VORBIS_too_many_channels
                    ? VorbisHeadersResult::Unsupported : VorbisHeadersResult::Invalid;
            }
            const auto info=stb_vorbis_get_info(decoder);
            stb_vorbis_close(decoder);
            return error==0 && consumed==int(pages.size()) && info.channels==identification[11] &&
                info.sample_rate==binary::ReadLittle32(identification.data()+12)
                ? VorbisHeadersResult::Valid : VorbisHeadersResult::Invalid;
        }
        return VorbisHeadersResult::Unsupported;
    }catch(...) {
        return VorbisHeadersResult::ResourceFailure;
    }
}
}
