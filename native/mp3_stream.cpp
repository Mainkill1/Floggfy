#include "mp3_stream.h"
#include "compressed_buffer.h"
#include "recording_limits.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
namespace history {
namespace {
constexpr std::size_t kFileLimit=kMaximumRecordingBytes,kTagLimit=16u*1024u*1024u;
// At most 41 2/3 frames/second across the supported MPEG versions and rates.
constexpr unsigned kFrameLimit=86400u*48000u/1152u;
bool Fail(std::string& error,const char* text) {error=text;return false;}
bool Syncsafe(const std::uint8_t* bytes,std::size_t& value) noexcept {
    if((bytes[0]|bytes[1]|bytes[2]|bytes[3])&0x80)return false;
    value=(std::size_t(bytes[0])<<21)|(std::size_t(bytes[1])<<14)|
          (std::size_t(bytes[2])<<7)|bytes[3];return true;
}
void Syncsafe(std::vector<std::uint8_t>& out,std::size_t value) {
    out.push_back(std::uint8_t(value>>21)&0x7f);out.push_back(std::uint8_t(value>>14)&0x7f);
    out.push_back(std::uint8_t(value>>7)&0x7f);out.push_back(std::uint8_t(value)&0x7f);
}
struct RawBytes {
    const std::uint8_t* data=nullptr;std::size_t length=0;
    std::size_t size() const noexcept{return length;}
    std::uint8_t operator[](std::size_t index) const noexcept{return data[index];}
};
template<class Bytes>
bool Match(const Bytes& bytes,std::size_t at,const char* value,std::size_t length) noexcept {
    if(at>bytes.size()||length>bytes.size()-at)return false;
    for(std::size_t index=0;index<length;++index)
        if(bytes[at+index]!=std::uint8_t(value[index]))return false;
    return true;
}
template<class Bytes>
bool Id3Range(const Bytes& bytes,std::size_t& begin,std::size_t& end,
              unsigned& version,std::string& error) {
    begin=end=0;version=0;
    const auto size=bytes.size();
    if(size<3||!Match(bytes,0,"ID3",3))return true;
    if(size<10)return Fail(error,"truncated ID3 header");
    version=bytes[3];if((version<2||version>4)||bytes[4]!=0)return Fail(error,"unsupported ID3 version");
    const std::uint8_t allowed=version==2?0xc0:version==3?0xe0:0xf0;
    if(bytes[5]&~allowed)return Fail(error,"invalid ID3 flags");
    std::uint8_t encoded[4]{bytes[6],bytes[7],bytes[8],bytes[9]};
    std::size_t payload=0;if(!Syncsafe(encoded,payload))return Fail(error,"invalid ID3 size");
    if(payload>kTagLimit)return Fail(error,"ID3 tag exceeds limit");
    const std::size_t footer=version==4&&(bytes[5]&0x10)?10:0;
    if(payload>size-10||footer>size-10-payload)return Fail(error,"truncated ID3 tag");
    begin=10;end=10+payload+footer;return true;
}
bool Text(const std::uint8_t* bytes,std::size_t size,std::string& out) {
    if(!size||bytes[0]!=3)return false;
    out.assign(reinterpret_cast<const char*>(bytes+1),size-1);
    return std::find(out.begin(),out.end(),'\0')==out.end();
}
const char* FieldForFrame(const char* id) noexcept {
    if(!std::memcmp(id,"TIT2",4))return "TITLE";
    if(!std::memcmp(id,"TPE1",4))return "ARTIST";
    if(!std::memcmp(id,"TALB",4))return "ALBUM";
    if(!std::memcmp(id,"TPE2",4))return "ALBUMARTIST";
    if(!std::memcmp(id,"TRCK",4))return "TRACKNUMBER";
    if(!std::memcmp(id,"TCON",4))return "GENRE";
    if(!std::memcmp(id,"TDRC",4))return "DATE";
    return nullptr;
}
const char* FrameForField(const std::string& name) noexcept {
    if(name=="TITLE")return "TIT2";
    if(name=="ARTIST")return "TPE1";
    if(name=="ALBUM")return "TALB";
    if(name=="ALBUMARTIST")return "TPE2";
    if(name=="TRACKNUMBER")return "TRCK";
    if(name=="GENRE")return "TCON";
    if(name=="DATE"||name=="YEAR")return "TDRC";
    return nullptr;
}
void Frame(std::vector<std::uint8_t>& tag,const char* id,const std::vector<std::uint8_t>& payload) {
    tag.insert(tag.end(),id,id+4);Syncsafe(tag,payload.size());tag.push_back(0);tag.push_back(0);
    tag.insert(tag.end(),payload.begin(),payload.end());
}
enum class Mp3Scan {Invalid,Incomplete,Complete,Boundary};
template<class Bytes>
Mp3Scan ScanMp3(const Bytes& bytes,Mp3Info& out,std::string& error,bool accept_truncated_tail,
                std::size_t* boundary=nullptr,const Mp3Info* resume=nullptr) {
    out={};error.clear();const auto size=bytes.size();
    if(size<4||size>kFileLimit){Fail(error,"invalid MP3 size");return Mp3Scan::Invalid;}
    std::size_t tag_begin=0,at=0;unsigned id3_version=0;
    if(resume&&resume->frames)at=resume->audio_offset+resume->audio_size;
    else if(!Id3Range(bytes,tag_begin,at,id3_version,error))return Mp3Scan::Invalid;
    if(at>size)return Mp3Scan::Invalid;
    std::size_t end=size;
    if(end-at>=128&&Match(bytes,end-128,"TAG",3))end-=128;
    if(end<at||(!resume&&end==at)){Fail(error,"MP3 has no audio frames");return Mp3Scan::Invalid;}
    unsigned expected_rate=resume?resume->rate:0,expected_channels=resume?resume->channels:0,frames=resume?resume->frames:0;
    std::uint64_t samples=resume?resume->samples:0;
    const std::size_t audio=resume&&resume->frames?resume->audio_offset:at;
    const auto truncated=[&](const char* message) {
        Fail(error,message);
        if(!accept_truncated_tail||!frames)return Mp3Scan::Invalid;
        out={expected_rate,expected_channels,frames,samples,audio,at-audio,false};
        return Mp3Scan::Incomplete;
    };
    while(at<end) {
        if(boundary&&frames&&end-at>128&&Match(bytes,at,"TAG",3)) {
            const auto next=at+128;
            if(Match(bytes,next,"ID3",std::min<std::size_t>(3,end-next))) {
                *boundary=next;out={expected_rate,expected_channels,frames,samples,audio,at-audio,true};
                return Mp3Scan::Boundary;
            }
        }
        if(boundary&&frames&&Match(bytes,at,"ID3",std::min<std::size_t>(3,end-at))) {
            *boundary=at;out={expected_rate,expected_channels,frames,samples,audio,at-audio,true};
            return Mp3Scan::Boundary;
        }
        if(end-at<4)return truncated("truncated MP3 frame header");
        const std::uint32_t h=(std::uint32_t(bytes[at])<<24)|(std::uint32_t(bytes[at+1])<<16)|
                              (std::uint32_t(bytes[at+2])<<8)|bytes[at+3];
        if((h&0xffe00000u)!=0xffe00000u){Fail(error,"invalid MP3 frame sync");return Mp3Scan::Invalid;}
        const unsigned version=(h>>19)&3,layer=(h>>17)&3,bitrate_index=(h>>12)&15,rate_index=(h>>10)&3;
        if(version==1){Fail(error,"reserved MPEG version");return Mp3Scan::Invalid;}
        if(layer!=1){Fail(error,"unsupported MPEG layer");return Mp3Scan::Invalid;}
        if(!bitrate_index||bitrate_index==15){Fail(error,"unsupported MP3 bitrate");return Mp3Scan::Invalid;}
        if(rate_index==3){Fail(error,"reserved MP3 sample rate");return Mp3Scan::Invalid;}
        static constexpr unsigned rates[3]={44100,48000,32000};
        static constexpr unsigned mpeg1[16]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
        static constexpr unsigned mpeg2[16]={0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};
        unsigned rate=rates[rate_index];if(version==2)rate/=2;else if(version==0)rate/=4;
        const unsigned bitrate=(version==3?mpeg1:mpeg2)[bitrate_index];
        const unsigned channels=((h>>6)&3)==3?1:2;
        const std::uint64_t scale=version==3?144000u:72000u;
        const std::uint64_t frame_size=scale*bitrate/rate+((h>>9)&1);
        if(frame_size<4){Fail(error,"invalid MP3 frame size");return Mp3Scan::Invalid;}
        if(frame_size>end-at)return truncated("truncated MP3 frame");
        if(!frames){expected_rate=rate;expected_channels=channels;}
        else if(rate!=expected_rate||channels!=expected_channels){Fail(error,"MP3 format changed midstream");return Mp3Scan::Invalid;}
        const unsigned per_frame=version==3?1152:576;
        if(samples>std::numeric_limits<std::uint64_t>::max()-per_frame||frames==kFrameLimit) {
            Fail(error,"MP3 frame count exceeds limit");return Mp3Scan::Invalid;
        }
        samples+=per_frame;++frames;at+=std::size_t(frame_size);
    }
    if(!frames||double(samples)/expected_rate>86400){Fail(error,"invalid MP3 duration");return Mp3Scan::Invalid;}
    out={expected_rate,expected_channels,frames,samples,audio,end-audio,true};error.clear();
    return Mp3Scan::Complete;
}
}
bool InspectMp3Source(const std::uint8_t* bytes,std::size_t size,Mp3Info& out,std::string& error) noexcept {
    out={};error.clear();
    try {
        if(!bytes)return Fail(error,"invalid MP3 size");
        return ScanMp3(RawBytes{bytes,size},out,error,false)==Mp3Scan::Complete;
    } catch(...) {out={};error.clear();return false;}
}
bool InspectMp3Source(const CompressedBuffer& bytes,Mp3Info& out,std::string& error) noexcept {
    out={};error.clear();
    try {return ScanMp3(bytes,out,error,false)==Mp3Scan::Complete;}
    catch(...) {out={};error.clear();return false;}
}
namespace {
struct Mp3Suffix {
    const CompressedBuffer& source;std::size_t offset,length;
    std::size_t size() const noexcept{return length;}
    std::uint8_t operator[](std::size_t at) const noexcept{return source[offset+at];}
};
}
bool FindMp3StreamStart(const CompressedBuffer& bytes,Mp3Info& cursor,std::size_t& offset) noexcept {
    try {
        Mp3Info next;std::string error;
        const auto status=ScanMp3(bytes,next,error,true,&offset,&cursor);
        if(next.frames)cursor=next;
        return status==Mp3Scan::Boundary;
    }catch(...){return false;}
}
bool InspectMp3Prefix(const CompressedBuffer& bytes,std::size_t offset,Mp3Info& info) noexcept {
    info={};if(offset>bytes.size())return false;
    try {
        std::string error;
        const auto status=ScanMp3(Mp3Suffix{bytes,offset,bytes.size()-offset},info,error,true);
        return status!=Mp3Scan::Invalid&&info.frames>0;
    }catch(...){info={};return false;}
}
bool InspectMp3Range(const CompressedBuffer& bytes,std::size_t offset,std::size_t length,Mp3Info& info) noexcept {
    info={};if(offset>bytes.size()||length>bytes.size()-offset)return false;
    try {std::string error;return ScanMp3(Mp3Suffix{bytes,offset,length},info,error,false)==Mp3Scan::Complete;}
    catch(...){info={};return false;}
}
bool MakeOwnedMp3(const std::uint8_t* source,std::size_t size,const Tags& tags,
                  std::vector<std::uint8_t>& out,std::string& error) {
    out.clear();Mp3Info info;if(!InspectMp3Source(source,size,info,error))return false;
    if(tags.fields.size()>65536||tags.cover.size()>4u*1024u*1024u)return Fail(error,"MP3 metadata exceeds limit");
    try {
        std::vector<std::uint8_t> frames;
        for(const auto& item:tags.fields) {
            if(item.first.empty()||item.first.size()>4096||item.second.size()>65536)
                return Fail(error,"MP3 metadata exceeds limit");
            std::vector<std::uint8_t> payload{3};
            if(const auto* id=FrameForField(item.first)) {
                payload.insert(payload.end(),item.second.begin(),item.second.end());Frame(frames,id,payload);
            } else {
                payload.insert(payload.end(),item.first.begin(),item.first.end());payload.push_back(0);
                payload.insert(payload.end(),item.second.begin(),item.second.end());Frame(frames,"TXXX",payload);
            }
            if(frames.size()>kTagLimit)return Fail(error,"MP3 metadata exceeds limit");
        }
        if(!tags.cover.empty()) {
            std::vector<std::uint8_t> payload{3};
            const char* mime=tags.cover.size()>=8&&!std::memcmp(tags.cover.data(),"\x89PNG\r\n\x1a\n",8)?"image/png":"image/jpeg";
            payload.insert(payload.end(),mime,mime+std::strlen(mime)+1);payload.push_back(3);payload.push_back(0);
            payload.insert(payload.end(),tags.cover.begin(),tags.cover.end());Frame(frames,"APIC",payload);
        }
        if(frames.size()>0x0fffffffu||info.audio_size>kFileLimit-10-frames.size())
            return Fail(error,"MP3 output exceeds limit");
        std::vector<std::uint8_t> result={'I','D','3',4,0,0};Syncsafe(result,frames.size());
        result.insert(result.end(),frames.begin(),frames.end());
        result.insert(result.end(),source+info.audio_offset,source+info.audio_offset+info.audio_size);
        out.swap(result);error.clear();return true;
    } catch(...) {out.clear();return Fail(error,"MP3 metadata allocation failed");}
}
Mp3InspectionResult InspectOwnedMp3Result(const std::uint8_t* bytes,std::size_t size,
                                          Mp3Info& info,Tags& tags,std::string& error) noexcept {
    info={};tags={};error.clear();
    try {
        const auto inspect=[&]() -> bool {
        if(!bytes)return Fail(error,"invalid MP3 size");
        const RawBytes source{bytes,size};
        if(ScanMp3(source,info,error,true)==Mp3Scan::Invalid)return false;
        std::size_t begin=0,end=0;unsigned version=0;
        if(!Id3Range(source,begin,end,version,error)||version!=4||begin!=10)
            return Fail(error,"owned MP3 requires ID3v2.4");
        const std::size_t payload_end=end;
        for(std::size_t at=begin;at<payload_end;) {
            if(payload_end-at<10)return Fail(error,"truncated ID3 frame");
            if(bytes[at]==0) {
                if(std::any_of(bytes+at,bytes+payload_end,[](std::uint8_t value){return value!=0;}))
                    return Fail(error,"invalid ID3 padding");
                break;
            }
            for(unsigned i=0;i<4;++i)if(!((bytes[at+i]>='A'&&bytes[at+i]<='Z')||(bytes[at+i]>='0'&&bytes[at+i]<='9')))
                return Fail(error,"invalid ID3 frame name");
            std::size_t length=0;if(!Syncsafe(bytes+at+4,length)||length>payload_end-at-10)
                return Fail(error,"invalid ID3 frame size");
            const char* id=reinterpret_cast<const char*>(bytes+at);const auto* data=bytes+at+10;
            if(const auto* name=FieldForFrame(id)) {
                std::string value;if(!Text(data,length,value))return Fail(error,"unsupported ID3 text");
                tags.fields.push_back({name,std::move(value)});
            } else if(!std::memcmp(id,"TXXX",4)) {
                if(!length||data[0]!=3)return Fail(error,"unsupported ID3 custom text");
                const auto* split=static_cast<const std::uint8_t*>(std::memchr(data+1,0,length-1));
                if(!split||split==data+1)return Fail(error,"invalid ID3 custom text");
                std::string name(reinterpret_cast<const char*>(data+1),reinterpret_cast<const char*>(split));
                std::string value(reinterpret_cast<const char*>(split+1),reinterpret_cast<const char*>(data+length));
                tags.fields.push_back({std::move(name),std::move(value)});
            } else if(!std::memcmp(id,"APIC",4)) {
                if(length<4||data[0]!=3)return Fail(error,"unsupported ID3 picture");
                const auto* mime=static_cast<const std::uint8_t*>(std::memchr(data+1,0,length-1));
                if(!mime||mime+2>data+length)return Fail(error,"invalid ID3 picture");
                const auto* description=static_cast<const std::uint8_t*>(std::memchr(mime+2,0,std::size_t(data+length-(mime+2))));
                if(!description||description+1>=data+length)return Fail(error,"invalid ID3 picture");
                tags.cover.assign(description+1,data+length);
            }
            at+=10+length;
        }
        error.clear();return true;
        };
        return inspect()?Mp3InspectionResult::Valid:Mp3InspectionResult::Invalid;
    } catch(...) {info={};tags={};error.clear();return Mp3InspectionResult::ResourceFailure;}
}
bool InspectOwnedMp3(const std::uint8_t* bytes,std::size_t size,Mp3Info& info,
                     Tags& tags,std::string& error) noexcept {
    return InspectOwnedMp3Result(bytes,size,info,tags,error)==Mp3InspectionResult::Valid;
}
}
