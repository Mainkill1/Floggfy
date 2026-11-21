#include "mp3_stream.h"
#include "mp3_header.h"
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
    std::size_t payload=0;if(!ReadId3Syncsafe(encoded,payload))return Fail(error,"invalid ID3 size");
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
constexpr struct {const char* id;const char* field;} text_frames[]={
    {"TIT2","TITLE"},{"TPE1","ARTIST"},{"TALB","ALBUM"},{"TPE2","ALBUMARTIST"},
    {"TRCK","TRACKNUMBER"},{"TCON","GENRE"},{"TDRC","DATE"}
};
const char* FieldForFrame(const char* id) noexcept {
    for(const auto& frame:text_frames)if(!std::memcmp(id,frame.id,4))return frame.field;
    return nullptr;
}
const char* FrameForField(const std::string& name) noexcept {
    if(name=="YEAR")return "TDRC";
    for(const auto& frame:text_frames)if(name==frame.field)return frame.id;
    return nullptr;
}
void Frame(std::vector<std::uint8_t>& tag,const char* id,const std::vector<std::uint8_t>& payload) {
    tag.insert(tag.end(),id,id+4);Syncsafe(tag,payload.size());tag.push_back(0);tag.push_back(0);
    tag.insert(tag.end(),payload.begin(),payload.end());
}
void TextFrame(std::vector<std::uint8_t>& frames,const std::string& key,const std::string& value) {
    const char* id=FrameForField(key);
    std::vector<std::uint8_t> payload{3};
    if(!id) {
        payload.insert(payload.end(),key.begin(),key.end());payload.push_back(0);
        id="TXXX";
    }
    payload.insert(payload.end(),value.begin(),value.end());
    Frame(frames,id,payload);
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
    unsigned declared_frames=resume?resume->declared_frames:0;
    std::size_t declared_bytes=resume?resume->declared_bytes:0;
    const std::size_t audio=resume&&resume->frames?resume->audio_offset:at;
    const auto truncated=[&](const char* message) {
        Fail(error,message);
        if(!accept_truncated_tail||!frames)return Mp3Scan::Invalid;
        out={expected_rate,expected_channels,frames,samples,audio,at-audio,false};
        out.declared_frames=declared_frames;out.declared_bytes=declared_bytes;
        return Mp3Scan::Incomplete;
    };
    const auto source_boundary=[&](std::size_t next) {
        *boundary=next;
        const bool complete=(!declared_frames||frames==declared_frames)&&
            (!declared_bytes||at-audio==declared_bytes);
        out={expected_rate,expected_channels,frames,samples,audio,at-audio,complete};
        out.declared_frames=declared_frames;out.declared_bytes=declared_bytes;
        return Mp3Scan::Boundary;
    };
    while(at<end) {
        if(boundary&&frames&&end-at>128&&Match(bytes,at,"TAG",3)) {
            const auto next=at+128;
            if(Match(bytes,next,"ID3",std::min<std::size_t>(3,end-next))) {
                return source_boundary(next);
            }
        }
        if(boundary&&frames&&Match(bytes,at,"ID3",std::min<std::size_t>(3,end-at))) {
            return source_boundary(at);
        }
        if(end-at<4)return truncated("truncated MP3 frame header");
        const Mp3FrameHeader header{(std::uint32_t(bytes[at])<<24)|(std::uint32_t(bytes[at+1])<<16)|
                                    (std::uint32_t(bytes[at+2])<<8)|bytes[at+3]};
        if(const auto* reason=header.Error()){Fail(error,reason);return Mp3Scan::Invalid;}
        const auto version=header.Version(),rate=header.Rate(),channels=header.Channels();
        const auto frame_size=header.FrameBytes();
        if(frame_size<4){Fail(error,"invalid MP3 frame size");return Mp3Scan::Invalid;}
        if(frame_size>end-at)return truncated("truncated MP3 frame");
        if(!frames) {
            const auto big=[&](size_t offset) {
                return (uint32_t(bytes[offset])<<24)|(uint32_t(bytes[offset+1])<<16)|
                    (uint32_t(bytes[offset+2])<<8)|bytes[offset+3];
            };
            const auto side=version==3?(channels==1?17u:32u):(channels==1?9u:17u);
            // LAME/FFmpeg keep this offset even when the frame has a CRC.
            const auto xing=at+4+side;
            if(xing+4<=at+frame_size&&(Match(bytes,xing,"Xing",4)||Match(bytes,xing,"Info",4))) {
                if(xing+8>at+frame_size)return truncated("truncated Xing flags");
                const auto flags=big(xing+4);size_t field=xing+8;
                if(flags&~15u)return truncated("invalid Xing flags");
                if(flags&1) {
                    if(field+4>at+frame_size)return truncated("truncated Xing frame count");
                    const auto count=big(field);field+=4;
                    // Xing's frame count excludes its informational frame.
                    if(count&&count<kFrameLimit)declared_frames=count+1;
                    else return truncated("invalid Xing frame count");
                }
                if(flags&2) {
                    if(field+4>at+frame_size)return truncated("truncated Xing byte count");
                    declared_bytes=big(field);field+=4;
                    if(declared_bytes<frame_size||declared_bytes>kFileLimit)return truncated("invalid Xing byte count");
                }
                if(flags&4)field+=100;
                if(flags&8)field+=4;
                if(field>at+frame_size)return truncated("truncated Xing fields");
            } else if(at+40<=at+frame_size&&Match(bytes,at+36,"VBRI",4)) {
                if(at+54>at+frame_size)return truncated("truncated VBRI declaration");
                if(bytes[at+40]!=0||bytes[at+41]!=1)return truncated("unsupported VBRI version");
                declared_bytes=big(at+46);declared_frames=big(at+50);
                if(!declared_frames||declared_frames>kFrameLimit||declared_bytes<frame_size||declared_bytes>kFileLimit)
                    return truncated("invalid VBRI declaration");
            }
        }
        if(!frames){expected_rate=rate;expected_channels=channels;}
        else if(rate!=expected_rate||channels!=expected_channels){Fail(error,"MP3 format changed midstream");return Mp3Scan::Invalid;}
        const unsigned per_frame=version==3?1152:576;
        if(samples>std::numeric_limits<std::uint64_t>::max()-per_frame||frames==kFrameLimit) {
            Fail(error,"MP3 frame count exceeds limit");return Mp3Scan::Invalid;
        }
        samples+=per_frame;++frames;at+=std::size_t(frame_size);
        if(declared_frames&&frames>declared_frames)return truncated("MP3 exceeds declared frame count");
    }
    if(!frames||double(samples)/expected_rate>86400){Fail(error,"invalid MP3 duration");return Mp3Scan::Invalid;}
    if((declared_frames&&frames<declared_frames)||(declared_bytes&&end-audio<declared_bytes))
        return truncated("MP3 ends before declared source length");
    if(declared_bytes&&end-audio>declared_bytes)return truncated("MP3 exceeds declared byte count");
    out={expected_rate,expected_channels,frames,samples,audio,end-audio,true};
    out.declared_frames=declared_frames;out.declared_bytes=declared_bytes;error.clear();
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
        const std::string* date=nullptr;
        const std::string* year=nullptr;
        for(const auto& item:tags.fields) {
            if(item.first=="DATE"&&!item.second.empty()&&!date)date=&item.second;
            if(item.first=="YEAR"&&!item.second.empty()&&!year)year=&item.second;
        }
        if(!date)date=year;
        for(const auto& item:tags.fields) {
            if(item.first.empty()||item.first.size()>4096||item.second.size()>65536)
                return Fail(error,"MP3 metadata exceeds limit");
            // DATE already contains the year; serialize one TDRC even when both are supplied.
            if((item.first=="DATE"||item.first=="YEAR") && &item.second!=date)continue;
            TextFrame(frames,item.first,item.second);
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
        std::vector<std::uint8_t> result;result.reserve(10+frames.size()+info.audio_size);
        result={'I','D','3',4,0,0};Syncsafe(result,frames.size());
        result.insert(result.end(),frames.begin(),frames.end());
        result.insert(result.end(),source+info.audio_offset,source+info.audio_offset+info.audio_size);
        out.swap(result);error.clear();return true;
    } catch(...) {out.clear();return Fail(error,"MP3 metadata allocation failed");}
}
// Insert ahead of existing frames so opaque frames, padding, artwork and audio survive.
bool AddMp3TextFrame(const std::vector<std::uint8_t>& source,const std::string& key,const std::string& value,
                     std::vector<std::uint8_t>& output) {
    size_t tag_size=0;
    if(source.size()<10 || source.size()>kFileLimit || std::memcmp(source.data(),"ID3\x04\x00\x00",6) ||
       !ReadId3Syncsafe(source.data()+6,tag_size) || tag_size>source.size()-10)return false;
    std::vector<std::uint8_t> frame;
    TextFrame(frame,key,value);
    if(frame.size()>kTagLimit || tag_size>kTagLimit-frame.size() || frame.size()>kFileLimit-source.size())return false;
    output.assign(source.begin(),source.begin()+6);
    Syncsafe(output,tag_size+frame.size());
    output.insert(output.end(),frame.begin(),frame.end());
    output.insert(output.end(),source.begin()+10,source.end());
    return true;
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
            std::size_t length=0;if(!ReadId3Syncsafe(bytes+at+4,length)||length>payload_end-at-10)
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
}
