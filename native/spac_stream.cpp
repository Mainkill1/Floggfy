#include "aac_stream.h"
#include <algorithm>
#include <array>
#include <utility>
namespace history { namespace {
constexpr size_t page_limit=65536,config_limit=64;
constexpr uint64_t frame_limit=uint64_t{1}<<20,audio_limit=uint64_t{512}<<20;
struct PageLayout {
    bool setup=false,shared=false;
    uint64_t base=0;
    size_t body=0,total=0,count=0;
    std::array<size_t,255> lengths{};
};
// Determine the next bounded prefix needed, without buffering another page.
// A page is magic/flags/count, optional frame index, laces, optional common
// packet prefix, payload, Fletcher32. There is no Ogg-style EOS flag.
AacParse Layout(const uint8_t* data,size_t size,PageLayout& out,size_t& needed) {
    out={};needed=6;
    for(size_t i=0;i<std::min(size,size_t{4});++i)
        if(data[i]!=uint8_t("SpAC"[i]))return AacParse::Invalid;
    if(size<needed)return AacParse::NeedMore;
    if(data[4]&~0x81u)return AacParse::Unsupported;
    out.setup=(data[4]&128)!=0;out.shared=(data[4]&1)!=0;
    const size_t laces=data[5];size_t cursor=6;
    if(!laces)return AacParse::Invalid;
    if(!out.setup) {
        needed=7;if(size<needed)return AacParse::NeedMore;
        const unsigned first=data[cursor];
        const size_t width=first<128?2:first<192?3:first<224?4:5;
        needed=cursor+width;if(size<needed)return AacParse::NeedMore;
        uint64_t value=width==2?first:width==3?(first&63):width==4?(first&31):0;
        for(size_t i=1;i<width;++i)value=(value<<8)|data[cursor+i];
        if(!value || value>frame_limit+1)return AacParse::Invalid;
        out.base=value-1;cursor+=width;
    }
    needed=cursor+laces;if(size<needed)return AacParse::NeedMore;
    size_t length=0,body_bytes=0;
    for(size_t i=0;i<laces;++i) {
        length+=data[cursor+i];
        if(data[cursor+i]!=255) {
            if(!length&&!out.shared)return AacParse::Invalid;
            out.lengths[out.count++]=length;body_bytes+=length;length=0;
        }
    }
    if(length || data[cursor+laces-1]==255 || !out.count)return AacParse::Invalid;
    out.body=cursor+laces+(out.shared?1:0);
    out.total=out.body+body_bytes+4;
    if(out.total>page_limit)return AacParse::Invalid;
    needed=out.total;
    return size<needed?AacParse::NeedMore:AacParse::Complete;
}
uint32_t Checksum(const uint8_t* bytes,size_t size) {
    uint32_t first=65535,second=65535;
    for(size_t at=0;at<size;) {
        const size_t end=std::min(size,at+704);
        while(at<end) {
            uint32_t word=bytes[at++];if(at<end)word|=uint32_t(bytes[at++])<<8;
            first+=word;second+=first;
        }
        first=(first&65535)+(first>>16);second=(second&65535)+(second>>16);
    }
    first=(first&65535)+(first>>16);second=(second&65535)+(second>>16);
    return (second<<16)|first;
}
uint32_t Little(const uint8_t* bytes,unsigned count) {
    uint32_t value=0;for(unsigned i=0;i<count;++i)value|=uint32_t(bytes[i])<<(i*8);return value;
}
struct Bits {
    const std::vector<uint8_t>& bytes;size_t at=0;
    bool Read(unsigned count,unsigned& value) {
        if(count>bytes.size()*8-at)return false;
        value=0;for(unsigned i=0;i<count;++i,++at)value=(value<<1)|((bytes[at/8]>>(7-at%8))&1);
        return true;
    }
};
bool Frequency(Bits& bits,unsigned& rate) {
    constexpr unsigned rates[]={96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350};
    unsigned index=0;if(!bits.Read(4,index))return false;
    if(index==15)return bits.Read(24,rate)&&rate>=7350&&rate<=192000;
    if(index>=sizeof(rates)/sizeof(rates[0]))return false;
    rate=rates[index];return true;
}
AacParse DecoderConfig(const AacInfo& info) {
    Bits bits{info.config};unsigned object=0,core_rate=0,channels=0;
    if(!bits.Read(5,object))return AacParse::Invalid;
    if(object!=2&&object!=5&&object!=29)return AacParse::Unsupported;
    if(!Frequency(bits,core_rate)||!bits.Read(4,channels)||!channels||channels>2)return AacParse::Invalid;
    if(object==29&&channels!=1)return AacParse::Unsupported;
    unsigned rendered_rate=core_rate,underlying=object;
    if(object!=2) {
        if(!Frequency(bits,rendered_rate)||!bits.Read(5,underlying))return AacParse::Invalid;
        if(underlying!=2)return AacParse::Unsupported;
    }
    unsigned short_frame=0,core_dependency=0,extension=0;
    if(!bits.Read(1,short_frame)||!bits.Read(1,core_dependency))return AacParse::Invalid;
    if(core_dependency)return AacParse::Unsupported;
    if(!bits.Read(1,extension))return AacParse::Invalid;
    if(extension)return AacParse::Unsupported;
    const bool stereo_ps=object==29&&channels==1&&info.channels==2;
    if(info.rate!=rendered_rate || (channels!=info.channels&&!stereo_ps))return AacParse::Invalid;
    if(rendered_rate!=core_rate && rendered_rate!=2*core_rate)return AacParse::Unsupported;
    const unsigned samples=(short_frame?960u:1024u)*(rendered_rate/core_rate);
    return info.samples_per_packet==samples?AacParse::Complete:AacParse::Invalid;
}
AacParse ReadConfig(const std::vector<uint8_t>& packet,AacInfo& info) {
    if(packet.size()<20)return AacParse::Invalid;
    if(packet[1])return AacParse::Unsupported;
    if(packet[2]!='m'||packet[3]!='p'||packet[4]!='4'||packet[5]!='a')return AacParse::Unsupported;
    const size_t extra=Little(packet.data()+18,2);
    if(extra<2||extra>config_limit||packet.size()!=20+extra)return AacParse::Invalid;
    info.total_samples=Little(packet.data()+6,4);info.channels=packet[10];info.rate=Little(packet.data()+11,3);
    info.priming=Little(packet.data()+14,2);info.samples_per_packet=Little(packet.data()+16,2);
    if(!info.total_samples||info.channels<1||info.channels>2||info.rate<7350||info.rate>192000||
       !info.samples_per_packet||info.samples_per_packet>8192||uint64_t(info.total_samples)>uint64_t(info.rate)*21600)return AacParse::Invalid;
    const uint64_t samples=uint64_t(info.total_samples)+info.priming;
    if((samples+info.samples_per_packet-1)/info.samples_per_packet>frame_limit)return AacParse::Invalid;
    info.config.assign(packet.begin()+20,packet.end());return DecoderConfig(info);
}
} // namespace
AacParse SpacStream::ParsePage(const uint8_t* page,size_t size,std::vector<std::vector<uint8_t>>* packets) {
    PageLayout layout;size_t needed=0;
    const auto measured=Layout(page,size,layout,needed);
    if(measured!=AacParse::Complete||layout.total!=size)return measured==AacParse::Complete?AacParse::Invalid:measured;
    if(Checksum(page,size-4)!=Little(page+size-4,4))return AacParse::Invalid;
    if(complete_ || (layout.setup&&configured_) || (!layout.setup&&!configured_))return AacParse::Invalid;
    if(!layout.setup&&layout.base!=frames_)return AacParse::Invalid;
    const uint64_t expected=configured_?(uint64_t(info.total_samples)+info.priming+info.samples_per_packet-1)/info.samples_per_packet:0;
    if(!layout.setup&&(layout.count>frame_limit-frames_||frames_+layout.count>expected))return AacParse::Invalid;
    size_t cursor=layout.body;bool found_config=false;
    for(size_t i=0;i<layout.count;++i) {
        const size_t length=layout.lengths[i];
        std::vector<uint8_t> packet;
        // Setup and extraction need bytes; capture validation only counts audio.
        if(layout.setup||packets) {
            packet.reserve(length+(layout.shared?1:0));
            if(layout.shared)packet.push_back(page[layout.body-1]);
            packet.insert(packet.end(),page+cursor,page+cursor+length);
        }
        if(layout.setup) {
            if(packet.empty())return AacParse::Invalid;
            if(packet[0]==1) {
                if(found_config)return AacParse::Invalid;
                const auto result=ReadConfig(packet,info);
                if(result!=AacParse::Complete)return result;
                found_config=true;
            }
        } else {
            const uint64_t bytes=length+(layout.shared?1:0);
            if(bytes>audio_limit-audio_bytes_)return AacParse::Invalid;
            audio_bytes_+=bytes;
            if(packets)packets->push_back(std::move(packet));
        }
        cursor+=length;
    }
    if(layout.setup) {
        if(!found_config)return AacParse::Invalid;
        configured_=true;
    } else {
        frames_+=layout.count;complete_=frames_==expected;
    }
    return complete_?AacParse::Complete:AacParse::NeedMore;
}
AacParse SpacStream::Push(const uint8_t* bytes,size_t size) {
    if(failed_)return failure_;
    auto fail=[&](AacParse result){failed_=true;failure_=result;pending_.clear();return result;};
    if((!bytes&&size)||(complete_&&size))return fail(AacParse::Invalid);
    if(size&&pending_.capacity()<page_limit)pending_.reserve(page_limit);
    while(size) {
        PageLayout layout;size_t needed=0;
        auto result=Layout(pending_.data(),pending_.size(),layout,needed);
        if(result==AacParse::Invalid||result==AacParse::Unsupported)return fail(result);
        if(pending_.size()<needed) {
            const size_t amount=std::min(size,needed-pending_.size());
            pending_.insert(pending_.end(),bytes,bytes+amount);bytes+=amount;size-=amount;
            result=Layout(pending_.data(),pending_.size(),layout,needed);
        }
        if(result==AacParse::Invalid||result==AacParse::Unsupported)return fail(result);
        if(result==AacParse::Complete) {
            result=ParsePage(pending_.data(),pending_.size(),nullptr);pending_.clear();
            if(result==AacParse::Invalid||result==AacParse::Unsupported)return fail(result);
            if(complete_&&size)return fail(AacParse::Invalid);
        }
    }
    if(complete_)std::vector<uint8_t>().swap(pending_);
    return complete_?AacParse::Complete:AacParse::NeedMore;
}
bool AacExtractionFitsBudget(size_t source_capacity,size_t source_size,size_t packet_count) {
    if(source_size>source_capacity || source_capacity>AacConstructionMemoryLimit ||
       !packet_count || packet_count>frame_limit) return false;
    // Original source + restored packets + exact reserved vector slots. A shared
    // prefix adds at most one byte per packet; allow 32 allocator bytes each.
    const uint64_t packet_objects=uint64_t(packet_count)*(sizeof(std::vector<uint8_t>)+33);
    const uint64_t required=uint64_t(source_capacity)+source_size+packet_objects+page_limit+config_limit;
    return required<=AacConstructionMemoryLimit;
}
bool ReadSpac(const std::vector<uint8_t>& source,AacInfo& info,std::vector<std::vector<uint8_t>>& packets,std::string& error) {
    // Capture already has a tighter global memory budget; this publication API
    // independently rejects oversized callers before allocating access units.
    if(source.empty()||source.size()>audio_limit){error="SpAC source exceeds bounds or is empty";return false;}
    if(info.config.capacity()>AacConstructionMemoryLimit ||
       packets.capacity()>AacConstructionMemoryLimit/sizeof(std::vector<uint8_t>)) {
        error="SpAC extraction memory budget exceeded";return false;
    }
    uint64_t retained=info.config.capacity()+uint64_t(packets.capacity())*sizeof(std::vector<uint8_t>);
    for(const auto& packet:packets) {
        if(retained>AacConstructionMemoryLimit || packet.capacity()>AacConstructionMemoryLimit-retained ||
           AacConstructionMemoryLimit-retained-packet.capacity()<32) {
            error="SpAC extraction memory budget exceeded";return false;
        }
        retained+=packet.capacity()+uint64_t{32};
    }
    if(source.capacity()>AacConstructionMemoryLimit || retained>AacConstructionMemoryLimit-source.capacity()) {
        error="SpAC extraction memory budget exceeded";return false;
    }
    SpacStream stream;std::vector<std::vector<uint8_t>> extracted;
    size_t cursor=0;
    while(cursor<source.size()) {
        PageLayout layout;size_t needed=0;
        const auto measured=Layout(source.data()+cursor,source.size()-cursor,layout,needed);
        if(measured!=AacParse::Complete){error="invalid, truncated or unsupported SpAC page";return false;}
        const auto result=stream.ParsePage(source.data()+cursor,layout.total,&extracted);
        if(result==AacParse::Invalid||result==AacParse::Unsupported){error="SpAC checksum, codec or frame continuity rejected";return false;}
        if(layout.setup) {
            const auto& config=stream.info;
            const auto expected=(uint64_t(config.total_samples)+config.priming+config.samples_per_packet-1)/config.samples_per_packet;
            if(!AacExtractionFitsBudget(size_t(source.capacity()+retained),source.size(),size_t(expected))) {
                error="SpAC extraction memory budget exceeded";return false;
            }
            extracted.reserve(size_t(expected));
        }
        cursor+=layout.total;
        if(stream.complete_&&cursor!=source.size()){error="SpAC has data after its final access unit";return false;}
    }
    if(!stream.Complete()){error="SpAC recording is incomplete";return false;}
    info=std::move(stream.info);packets=std::move(extracted);error.clear();return true;
}
} // namespace history
