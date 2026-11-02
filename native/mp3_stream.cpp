#include "mp3_stream.h"
#include "compressed_buffer.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
namespace history {
namespace {
constexpr std::size_t kFileLimit=512u*1024u*1024u,kTagLimit=16u*1024u*1024u;
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
bool Id3Range(const std::uint8_t* bytes,std::size_t size,std::size_t& begin,std::size_t& end,
              unsigned& version,std::string& error) noexcept {
    begin=end=0;version=0;
    if(size<3||std::memcmp(bytes,"ID3",3))return true;
    if(size<10)return Fail(error,"truncated ID3 header");
    version=bytes[3];if((version!=3&&version!=4)||bytes[4]!=0)return Fail(error,"unsupported ID3 version");
    const std::uint8_t allowed=version==3?0xe0:0xf0;
    if(bytes[5]&~allowed)return Fail(error,"invalid ID3 flags");
    std::size_t payload=0;if(!Syncsafe(bytes+6,payload))return Fail(error,"invalid ID3 size");
    if(payload>kTagLimit)return Fail(error,"ID3 tag exceeds limit");
    const std::size_t footer=version==4&&(bytes[5]&0x10)?10:0;
    if(payload>size-10||footer>size-10-payload)return Fail(error,"truncated ID3 tag");
    begin=10;end=10+payload+footer;return true;
}
bool Text(const std::uint8_t* bytes,std::size_t size,std::string& out) noexcept {
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
}
bool InspectMp3Source(const std::uint8_t* bytes,std::size_t size,Mp3Info& out,std::string& error) noexcept {
    out={};error.clear();
    if(!bytes||size<4||size>kFileLimit)return Fail(error,"invalid MP3 size");
    std::size_t tag_begin=0,at=0;unsigned id3_version=0;
    if(!Id3Range(bytes,size,tag_begin,at,id3_version,error))return false;
    std::size_t end=size;
    if(end-at>=128&&!std::memcmp(bytes+end-128,"TAG",3))end-=128;
    if(end<=at)return Fail(error,"MP3 has no audio frames");
    unsigned expected_rate=0,expected_channels=0,frames=0;std::uint64_t samples=0;
    const std::size_t audio=at;
    while(at<end) {
        if(end-at<4)return Fail(error,"truncated MP3 frame header");
        const std::uint32_t h=(std::uint32_t(bytes[at])<<24)|(std::uint32_t(bytes[at+1])<<16)|
                              (std::uint32_t(bytes[at+2])<<8)|bytes[at+3];
        if((h&0xffe00000u)!=0xffe00000u)return Fail(error,"invalid MP3 frame sync");
        const unsigned version=(h>>19)&3,layer=(h>>17)&3,bitrate_index=(h>>12)&15,rate_index=(h>>10)&3;
        if(version==1)return Fail(error,"reserved MPEG version");
        if(layer!=1)return Fail(error,"unsupported MPEG layer");
        if(!bitrate_index||bitrate_index==15)return Fail(error,"unsupported MP3 bitrate");
        if(rate_index==3)return Fail(error,"reserved MP3 sample rate");
        static constexpr unsigned rates[3]={44100,48000,32000};
        static constexpr unsigned mpeg1[16]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
        static constexpr unsigned mpeg2[16]={0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};
        unsigned rate=rates[rate_index];if(version==2)rate/=2;else if(version==0)rate/=4;
        const unsigned bitrate=(version==3?mpeg1:mpeg2)[bitrate_index];
        const unsigned channels=((h>>6)&3)==3?1:2;
        const std::uint64_t scale=version==3?144000u:72000u;
        const std::uint64_t frame_size=scale*bitrate/rate+((h>>9)&1);
        if(frame_size<4||frame_size>end-at)return Fail(error,"truncated MP3 frame");
        if(!frames){expected_rate=rate;expected_channels=channels;}
        else if(rate!=expected_rate||channels!=expected_channels)return Fail(error,"MP3 format changed midstream");
        const unsigned per_frame=version==3?1152:576;
        if(samples>std::numeric_limits<std::uint64_t>::max()-per_frame||frames==1000000)
            return Fail(error,"MP3 frame count exceeds limit");
        samples+=per_frame;++frames;at+=std::size_t(frame_size);
    }
    if(!frames||double(samples)/expected_rate>86400)return Fail(error,"invalid MP3 duration");
    out={expected_rate,expected_channels,frames,samples,audio,end-audio};return true;
}
bool InspectMp3Source(const CompressedBuffer& bytes,Mp3Info& out,std::string& error) noexcept {
    out={};error.clear();const auto size=bytes.size();
    if(size<4||size>kFileLimit)return Fail(error,"invalid MP3 size");
    std::size_t at=0;
    if(size>=3&&bytes[0]=='I'&&bytes[1]=='D'&&bytes[2]=='3') {
        if(size<10)return Fail(error,"truncated ID3 header");
        const unsigned version=bytes[3];
        if((version!=3&&version!=4)||bytes[4]!=0)return Fail(error,"unsupported ID3 version");
        const std::uint8_t allowed=version==3?0xe0:0xf0;
        if(bytes[5]&~allowed)return Fail(error,"invalid ID3 flags");
        std::uint8_t encoded[4]{bytes[6],bytes[7],bytes[8],bytes[9]};std::size_t payload=0;
        if(!Syncsafe(encoded,payload))return Fail(error,"invalid ID3 size");
        if(payload>kTagLimit)return Fail(error,"ID3 tag exceeds limit");
        const std::size_t footer=version==4&&(bytes[5]&0x10)?10:0;
        if(payload>size-10||footer>size-10-payload)return Fail(error,"truncated ID3 tag");
        at=10+payload+footer;
    }
    std::size_t end=size;
    if(end-at>=128&&bytes[end-128]=='T'&&bytes[end-127]=='A'&&bytes[end-126]=='G')end-=128;
    if(end<=at)return Fail(error,"MP3 has no audio frames");
    const auto audio=at;unsigned expected_rate=0,expected_channels=0,frames=0;std::uint64_t samples=0;
    while(at<end) {
        if(end-at<4)return Fail(error,"truncated MP3 frame header");
        const std::uint32_t h=(std::uint32_t(bytes[at])<<24)|(std::uint32_t(bytes[at+1])<<16)|
                              (std::uint32_t(bytes[at+2])<<8)|bytes[at+3];
        if((h&0xffe00000u)!=0xffe00000u)return Fail(error,"invalid MP3 frame sync");
        const unsigned version=(h>>19)&3,layer=(h>>17)&3,bitrate_index=(h>>12)&15,rate_index=(h>>10)&3;
        if(version==1)return Fail(error,"reserved MPEG version");
        if(layer!=1)return Fail(error,"unsupported MPEG layer");
        if(!bitrate_index||bitrate_index==15)return Fail(error,"unsupported MP3 bitrate");
        if(rate_index==3)return Fail(error,"reserved MP3 sample rate");
        static constexpr unsigned rates[3]={44100,48000,32000};
        static constexpr unsigned mpeg1[16]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
        static constexpr unsigned mpeg2[16]={0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};
        unsigned rate=rates[rate_index];if(version==2)rate/=2;else if(version==0)rate/=4;
        const unsigned bitrate=(version==3?mpeg1:mpeg2)[bitrate_index];
        const unsigned channels=((h>>6)&3)==3?1:2;
        const std::uint64_t frame_size=(version==3?144000u:72000u)*bitrate/rate+((h>>9)&1);
        if(frame_size<4||frame_size>end-at)return Fail(error,"truncated MP3 frame");
        if(!frames){expected_rate=rate;expected_channels=channels;}
        else if(rate!=expected_rate||channels!=expected_channels)return Fail(error,"MP3 format changed midstream");
        const unsigned per_frame=version==3?1152:576;
        if(samples>std::numeric_limits<std::uint64_t>::max()-per_frame||frames==1000000)
            return Fail(error,"MP3 frame count exceeds limit");
        samples+=per_frame;++frames;at+=std::size_t(frame_size);
    }
    if(!frames||double(samples)/expected_rate>86400)return Fail(error,"invalid MP3 duration");
    out={expected_rate,expected_channels,frames,samples,audio,end-audio};return true;
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
bool InspectOwnedMp3(const std::uint8_t* bytes,std::size_t size,Mp3Info& info,
                     Tags& tags,std::string& error) noexcept {
    tags={};if(!InspectMp3Source(bytes,size,info,error))return false;
    std::size_t begin=0,end=0;unsigned version=0;
    if(!Id3Range(bytes,size,begin,end,version,error)||version!=4||begin!=10)
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
}
}
