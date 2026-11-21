#include "../tooling/binary.h"
#include "marked_recording.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <string_view>
#include <vector>

namespace history {
namespace {
constexpr uint64_t metadata_limit=16*1024*1024;
constexpr unsigned operation_limit=200000;
constexpr size_t key_limit=32,value_limit=128;
bool Syncsafe(const uint8_t* p,uint32_t& value) {
    if((p[0]|p[1]|p[2]|p[3])&128)return false;
    value=uint32_t(p[0])<<21|uint32_t(p[1])<<14|uint32_t(p[2])<<7|p[3];
    return true;
}

struct Input {
    uint64_t size;
    const MarkedReadAt& read;
    uint64_t budget=metadata_limit;
    unsigned calls=operation_limit;
    bool Get(uint64_t at,void* dest,size_t length) {
        if(at>size || length>size-at || length>budget || !calls)return false;
        budget-=length;
        --calls;
        return !length || read(at,dest,length);
    }
};
std::string Upper(std::string_view text) {
    std::string key(text);
    for(char& c:key)if(c>='a' && c<='z')c=char(c-'a'+'A');
    return key;
}
bool Relevant(const std::string& key) {
    return key=="SPOTIFY_URI" || key=="HISTORY_SAVED_UTC" || key=="HISTORY_CAPTURE" ||
           key=="HISTORY_COMPLETE_STREAM" || key=="HISTORY_COMPLETE_LISTEN";
}
struct Fields {
    std::map<std::string,std::string> values;
    bool Set(const MarkedReadAt& get,uint64_t at,uint64_t bytes,const std::string& key) {
        if(!Relevant(key))return true;
        if(bytes>value_limit)return false;
        std::string value(size_t(bytes),'\0');
        if(bytes && !get(at,value.data(),value.size()))return false;
        const auto existing=values.find(key);
        if(existing!=values.end())return existing->second==value;
        values.emplace(key,std::move(value));
        return true;
    }
    std::string Value(const char* key) const {
        const auto i=values.find(key);
        return i==values.end()?std::string{}:i->second;
    }
};
// Read lengths and key prefixes of variable-size fields, skipping the rest of
// irrelevant values (including base64 artwork comments and long lyrics).
bool Comments(const MarkedReadAt& get,uint64_t size,Fields& fields,bool vorbis) {
    if(size>metadata_limit)return false;
    uint64_t at=0;
    auto length=[&](uint32_t& n) {
        uint8_t bytes[4];
        if(size-at<sizeof(bytes) || !get(at,bytes,sizeof(bytes)))return false;
        at+=sizeof(bytes);
        n=binary::ReadLittle32(bytes);
        return true;
    };
    uint32_t vendor=0,count=0;
    if(!length(vendor) || vendor>size-at)return false;
    at+=vendor;
    if(!length(count) || count>100000 || count>(size-at)/4)return false;
    for(uint32_t i=0;i<count;++i) {
        uint32_t bytes=0;
        if(!length(bytes) || bytes>size-at)return false;
        char prefix[key_limit];
        const auto probe=std::min<size_t>(bytes,sizeof(prefix));
        if(probe && !get(at,prefix,probe))return false;
        const auto split=std::string_view(prefix,probe).find('=');
        if(split!=std::string_view::npos &&
           !fields.Set(get,at+split+1,bytes-split-1,Upper({prefix,split})))return false;
        at+=bytes;
    }
    uint8_t framing=0;
    return vorbis?size-at==1 && get(at,&framing,1) && framing==1:at==size;
}

bool Flac(Input& input,Fields& fields) {
    uint64_t at=4;
    bool last=false,comments=false;
    do {
        uint8_t header[4];
        if(!input.Get(at,header,sizeof(header)))return false;
        const unsigned type=header[0]&127;
        last=(header[0]&128)!=0;
        const auto bytes=binary::ReadBig32(header)&0xffffff;
        if(type==127 || (at==4?(type!=0 || bytes!=34):type==0))return false;
        at+=sizeof(header);
        if(bytes>input.size-at || at+bytes>metadata_limit)return false;
        if(type==4) {
            if(comments || !Comments([&](uint64_t p,void* d,size_t n){return input.Get(at+p,d,n);},bytes,fields,false))
                return false;
            comments=true;
        }
        at+=bytes;
    } while(!last);
    uint8_t frame[2];
    // A frame has a variable header, at least one subframe, and a CRC.
    return comments && input.size-at>=10 && input.Get(at,frame,sizeof(frame)) &&
           frame[0]==0xff && (frame[1]&0xfe)==0xf8;
}

struct PacketSpan {uint64_t offset,end;};
bool Ogg(Input& input,Fields& fields) {
    uint64_t at=0,packet_size=0;
    uint32_t serial=0,sequence=0;
    bool continued=false;
    unsigned packets=0;
    uint8_t audio_type=0;
    std::vector<PacketSpan> packet;
    while(at<input.size && at<metadata_limit) {
        uint8_t header[27],laces[255];
        if(!input.Get(at,header,sizeof(header)) || std::memcmp(header,"OggS",4) || header[4])return false;
        if(!sequence)serial=binary::ReadLittle32(header+14);
        const auto flags=header[5];
        if(binary::ReadLittle32(header+14)!=serial || binary::ReadLittle32(header+18)!=sequence ||
           (sequence?((flags&~5u) || bool(flags&1)!=continued):flags!=2) || !header[26])return false;
        const auto count=header[26];
        at+=sizeof(header);
        if(!input.Get(at,laces,count))return false;
        at+=count;
        uint64_t page_size=0;
        for(unsigned i=0;i<count;++i)page_size+=laces[i];
        if(page_size>input.size-at)return false;
        for(unsigned i=0;i<count;++i) {
            const auto length=laces[i];
            if(packet_size+length>metadata_limit)return false;
            if(length && packets==3 && !packet_size && !input.Get(at,&audio_type,1))return false;
            packet_size+=length;
            if(length && packets<3) {
                if(i && !packet.empty() && laces[i-1]==255)packet.back().end=packet_size;
                else packet.push_back({at,packet_size});
            }
            at+=length;
            continued=length==255;
            if(continued)continue;
            if(packets==3)return packet_size && !(audio_type&1);
            const MarkedReadAt get=[&](uint64_t pos,void* dest,size_t bytes) {
                auto span=std::upper_bound(packet.begin(),packet.end(),pos,
                    [](uint64_t value,const PacketSpan& entry){return value<entry.end;});
                auto* output=static_cast<uint8_t*>(dest);
                while(bytes && span!=packet.end()) {
                    const auto start=span==packet.begin()?0:(span-1)->end;
                    const auto n=size_t(std::min<uint64_t>(bytes,span->end-pos));
                    if(!input.Get(span->offset+pos-start,output,n))return false;
                    bytes-=n;pos+=n;output+=n;++span;
                }
                return !bytes;
            };
            uint8_t signature[7];
            constexpr uint8_t packet_type[]={1,3,5};
            if(packet_size<sizeof(signature) || !get(0,signature,sizeof(signature)) ||
               signature[0]!=packet_type[packets] || std::memcmp(signature+1,"vorbis",6))return false;
            if(packets==0 && packet_size!=30)return false;
            if(packets==1 &&
               !Comments([&](uint64_t p,void* d,size_t n){return get(p+7,d,n);},packet_size-7,fields,true))return false;
            if(packets==2 && packet_size==sizeof(signature))return false;
            ++packets;packet.clear();packet_size=0;
        }
        if(flags&4)return false;
        ++sequence;
    }
    return false;
}

// Check the first Layer III frame and its computed extent without reading it.
bool Mp3Audio(Input& input,uint64_t at) {
    uint8_t header[4];
    if(!input.Get(at,header,sizeof(header)))return false;
    const auto bits=binary::ReadBig32(header);
    const unsigned version=(bits>>19)&3,layer=(bits>>17)&3;
    const unsigned bitrate=(bits>>12)&15,rate=(bits>>10)&3;
    if((bits&0xffe00000u)!=0xffe00000u || version==1 || layer!=1 || !bitrate || bitrate==15 || rate==3)
        return false;
    constexpr unsigned rates[]={44100,48000,32000};
    constexpr unsigned mpeg1[]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320};
    constexpr unsigned mpeg2[]={0,8,16,24,32,40,48,56,64,80,96,112,128,144,160};
    const auto frequency=rates[rate]/(version==3?1:version==2?2:4);
    const auto bytes=(version==3?144000*mpeg1[bitrate]:72000*mpeg2[bitrate])/frequency+((bits>>9)&1);
    return bytes<=input.size-at;
}
bool Mp3(Input& input,Fields& fields) {
    uint8_t header[10];
    uint32_t bytes=0;
    if(!input.Get(0,header,sizeof(header)) || header[3]!=4 || header[4] || header[5] ||
       !Syncsafe(header+6,bytes) || bytes>metadata_limit || bytes>=input.size-sizeof(header))return false;
    const uint64_t end=sizeof(header)+bytes;
    if(!Mp3Audio(input,end))return false;
    for(uint64_t at=sizeof(header);at<end;) {
        uint8_t frame[10];
        uint32_t length=0;
        if(end-at<sizeof(frame) || !input.Get(at,frame,sizeof(frame)))return false;
        if(!frame[0]) {
            uint8_t padding[1024];
            while(at<end) {
                const auto n=size_t(std::min<uint64_t>(end-at,sizeof(padding)));
                if(!input.Get(at,padding,n) || std::any_of(padding,padding+n,[](uint8_t c){return c!=0;}))return false;
                at+=n;
            }
            break;
        }
        at+=sizeof(frame);
        if(!Syncsafe(frame+4,length) || length>end-at)return false;
        if(!std::memcmp(frame,"TXXX",4)) {
            char prefix[key_limit+1];
            const auto probe=std::min<size_t>(length,sizeof(prefix));
            if(!probe || frame[8] || frame[9] || !input.Get(at,prefix,probe) || prefix[0]!=3)return false;
            const auto split=std::string_view(prefix+1,probe-1).find('\0');
            if(split!=std::string_view::npos &&
               !fields.Set([&](uint64_t p,void* d,size_t n){return input.Get(p,d,n);},
                           at+split+2,length-split-2,Upper({prefix+1,split})))return false;
        }
        at+=length;
    }
    return true;
}

struct Box {
    uint64_t body=0,end=0;
    char kind[4]{};
    bool Is(const char* name) const {return !std::memcmp(kind,name,4);}
};
bool ReadBox(Input& input,uint64_t at,uint64_t end,Box& box) {
    uint8_t header[8];
    if(end-at<sizeof(header) || !input.Get(at,header,sizeof(header)))return false;
    std::memcpy(box.kind,header+4,4);
    uint64_t length=binary::ReadBig32(header),head=8;
    if(length==1) {
        if(end-at<16 || !input.Get(at+8,header,sizeof(header)))return false;
        length=uint64_t(binary::ReadBig32(header))<<32|binary::ReadBig32(header+4);
        head=16;
    } else if(!length)length=end-at;
    if(length<head || length>end-at)return false;
    box.body=at+head;box.end=at+length;
    return true;
}
bool Freeform(Input& input,const Box& entry,Fields& fields) {
    std::string key;
    bool itunes=false,seen_mean=false,seen_name=false;
    for(uint64_t at=entry.body;at<entry.end;) {
        Box box;
        if(!ReadBox(input,at,entry.end,box))return false;
        if(box.Is("mean") || box.Is("name")) {
            auto& seen=box.Is("mean")?seen_mean:seen_name;
            if(seen)return false;
            seen=true;
            const auto bytes=box.end-box.body;
            char text[4+key_limit];
            if(bytes>=4 && bytes<=sizeof(text)) {
                if(!input.Get(box.body,text,size_t(bytes)))return false;
                if(binary::ReadBig32(reinterpret_cast<const uint8_t*>(text))==0) {
                    std::string_view name(text+4,size_t(bytes)-4);
                    if(box.Is("mean"))itunes=name=="com.apple.iTunes";
                    else key=Upper(name);
                }
            }
        }
        at=box.end;
    }
    if(!itunes || !Relevant(key))return true;
    bool found=false;
    for(uint64_t at=entry.body;at<entry.end;) {
        Box box;
        if(!ReadBox(input,at,entry.end,box))return false;
        if(box.Is("data")) {
            uint8_t info[8];
            if(box.end-box.body<sizeof(info) || !input.Get(box.body,info,sizeof(info)) ||
               binary::ReadBig32(info)!=1 || binary::ReadBig32(info+4)!=0 ||
               !fields.Set([&](uint64_t p,void* d,size_t n){return input.Get(p,d,n);},
                           box.body+8,box.end-box.body-8,key))return false;
            found=true;
        }
        at=box.end;
    }
    return found;
}
bool Mp4(Input& input,uint64_t at,uint64_t end,unsigned level,Fields& fields) {
    const char* names[]={"moov","udta","meta","ilst","----"};
    bool seen=false,media=false;
    while(at<end) {
        Box box;
        if(!ReadBox(input,at,end,box))return false;
        if(level==0 && box.Is("mdat") && box.body<box.end)media=true;
        if(box.Is(names[level])) {
            if(level==4) {
                if(!Freeform(input,box,fields))return false;
            } else {
                if(seen)return false;
                uint64_t child=box.body;
                if(level==2) {
                    uint8_t version[4];
                    if(box.end-child<sizeof(version) || !input.Get(child,version,sizeof(version)) || binary::ReadBig32(version))return false;
                    child+=sizeof(version);
                }
                if(!Mp4(input,child,box.end,level+1,fields))return false;
            }
            seen=true;
        }
        at=box.end;
    }
    return level==0?seen && media:seen || level==4;
}
}
bool ReadMarkedRecording(uint64_t bytes,const MarkedReadAt& read,MarkedRecording& out,std::string& error) {
    out={};
    error.clear();
    Input input{bytes,read};
    uint8_t magic[8];
    const char* capture=nullptr;
    Fields fields;
    bool valid=false;
    if(input.Get(0,magic,sizeof(magic))) {
        if(!std::memcmp(magic,"fLaC",4)) {
            capture="native compressed FLAC input";valid=Flac(input,fields);
        } else if(!std::memcmp(magic,"OggS",4)) {
            capture="native compressed Ogg pages";valid=Ogg(input,fields);
        } else if(!std::memcmp(magic,"ID3",3)) {
            capture="native compressed MP3 input";valid=Mp3(input,fields);
        } else if(!std::memcmp(magic+4,"ftyp",4)) {
            capture="native compressed SpAC AAC packets";valid=Mp4(input,0,bytes,0,fields);
        }
    }
    if(!valid) {
        error="unsupported or invalid recording metadata";
        return false;
    }
    auto saved=fields.Value("HISTORY_SAVED_UTC");
    if(fields.values.count("HISTORY_SAVED_UTC") && !ValidMarkedDate(saved)) {
        error="invalid HISTORY_SAVED_UTC";
        return false;
    }
    auto uri=fields.Value("SPOTIFY_URI");
    if(!ValidSpotifyTrackUri(uri))uri.clear();
    const bool complete=fields.Value("HISTORY_COMPLETE_STREAM")=="1" || fields.Value("HISTORY_COMPLETE_LISTEN")=="1";
    const bool owned=complete && fields.Value("HISTORY_CAPTURE")==capture;
    out={std::move(uri),std::move(saved),bytes,0,complete,owned};
    return true;
}
}
