#include "recording_file.h"
#include "ogg_history_core.h"
#include "image_info.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
namespace history {
namespace {
constexpr size_t metadata_limit=16*1024*1024;
uint32_t Little(const uint8_t* p) { return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24); }
std::string Field(const Tags& tags,const char* name) {
    for(const auto& field:tags.fields) if(field.first==name) return field.second;
    return {};
}
uint32_t Big(const uint8_t* bytes) {return (uint32_t(bytes[0])<<24)|(uint32_t(bytes[1])<<16)|(uint32_t(bytes[2])<<8)|bytes[3];}
void Picture(const uint8_t* bytes,size_t size,RecordingFile& record) {
    record.has_artwork=true;
    // Only front covers are rewritten. Unknown layouts and other picture
    // types remain opaque so an album-art refresh cannot discard a booklet.
    if(size<4 || Big(bytes)!=3){record.artwork_edit_safe=false;return;}
    size_t at=4;
    if(size-at<4)return;
    const auto mime_size=Big(bytes+at);at+=4;if(mime_size>size-at)return;
    const bool png=mime_size==9 && !memcmp(bytes+at,"image/png",9);
    const bool jpeg=mime_size==10 && !memcmp(bytes+at,"image/jpeg",10);at+=mime_size;
    if(!png && !jpeg){record.artwork_edit_safe=false;return;}
    if(size-at<4)return;
    const auto description_size=Big(bytes+at);at+=4;if(description_size>size-at)return;at+=description_size;
    if(size-at<20)return;
    at+=16;const auto length=Big(bytes+at);at+=4;
    if(length!=size-at)return;
    if(length>4*1024*1024){record.artwork_edit_safe=false;return;}
    // A bad/missing signature is confirmed corrupt for its declared format.
    // A recognizable but unsupported/bounds-rejected image is opaque: never
    // mistake an inspection limit for permission to replace a good cover.
    if((png && (length<8 || memcmp(bytes+at,"\x89PNG\r\n\x1a\n",8))) ||
       (jpeg && (length<2 || bytes[at]!=0xff || bytes[at+1]!=0xd8)))return;
    RecordingArtwork image;
    if(!ReadRecordingArtwork(std::vector<uint8_t>(bytes+at,bytes+at+length),image)){record.artwork_edit_safe=false;return;}
    record.artworks.push_back(image);
    if(!record.artwork.valid || uint64_t(image.width)*image.height>uint64_t(record.artwork.width)*record.artwork.height)
        record.artwork=std::move(image);
}
bool Base64Picture(const std::string& text,std::vector<uint8_t>& output) {
    if(text.size()>metadata_limit)return false;
    output.clear();output.reserve(text.size()/4*3);
    int group[4]{};unsigned count=0;bool ended=false;
    auto digit=[](unsigned char c)->int {
        if(c>='A'&&c<='Z')return c-'A';
        if(c>='a'&&c<='z')return c-'a'+26;
        if(c>='0'&&c<='9')return c-'0'+52;
        if(c=='+')return 62;
        if(c=='/')return 63;
        return c=='='?-2:-1;
    };
    for(unsigned char c:text) {
        if(c==' ' || c=='\r' || c=='\n' || c=='\t')continue;
        if(ended)return false;
        const int value=digit(c);if(value==-1)return false;group[count++]=value;
        if(count<4)continue;
        count=0;if(group[0]<0 || group[1]<0)return false;
        output.push_back(uint8_t((group[0]<<2)|(group[1]>>4)));
        if(group[2]==-2){if(group[3]!=-2 || (group[1]&15))return false;ended=true;continue;}
        if(group[2]<0)return false;
        output.push_back(uint8_t((group[1]<<4)|(group[2]>>2)));
        if(group[3]==-2){if(group[2]&3)return false;ended=true;continue;}
        if(group[3]<0)return false;
        output.push_back(uint8_t((group[2]<<6)|group[3]));
    }
    return count==0 && !output.empty();
}
bool Comments(const uint8_t* b,size_t size,RecordingFile& record,bool vorbis) {
    size_t p=0;
    auto string=[&](std::string& value) {
        if(size-p<4) return false;
        size_t n=Little(b+p); p+=4;
        if(n>size-p) return false;
        value.assign(reinterpret_cast<const char*>(b+p),n); p+=n; return true;
    };
    std::string vendor;
    if(size>metadata_limit || !string(vendor) || size-p<4) return false;
    uint32_t count=Little(b+p); p+=4;
    if(count>100000 || count>(size-p)/4) return false;
    for(uint32_t i=0;i<count;i++) {
        std::string value;
        if(!string(value)) return false;
        auto equals=value.find('=');
        if(equals==std::string::npos) continue;
        auto key=value.substr(0,equals);
        for(char& c:key) if(c>='a'&&c<='z') c=char(c-'a'+'A');
        // Artwork can dwarf all textual metadata and is irrelevant to identity.
        if(key=="METADATA_BLOCK_PICTURE") {
            if(equals+1<value.size()) {
                record.has_artwork=true;std::vector<uint8_t> picture;
                if(Base64Picture(value.substr(equals+1),picture))Picture(picture.data(),picture.size(),record);
                else record.artwork_edit_safe=false;
            }
        } else record.tags.fields.emplace_back(std::move(key),value.substr(equals+1));
    }
    return vorbis ? p+1==size && b[p]==1 : p==size;
}
uint8_t Crc8(const uint8_t* b,size_t n) {
    uint8_t crc=0;
    while(n--) { crc^=*b++; for(unsigned j=0;j<8;j++) crc=uint8_t((crc<<1)^((crc&128)?7:0)); }
    return crc;
}
uint16_t Crc16(const uint8_t* b,size_t n) {
    uint16_t crc=0;
    while(n--) { crc^=uint16_t(*b++)<<8; for(unsigned j=0;j<8;j++) crc=uint16_t((crc<<1)^((crc&32768)?0x8005:0)); }
    return crc;
}
struct Bits {
    const uint8_t* bytes; size_t size,position=0; bool ok=true;
    uint64_t Read(unsigned count) {
        if(count>64 || count>size*8-position) { ok=false; return 0; }
        uint64_t result=0;
        while(count--) { result=(result<<1)|((bytes[position/8]>>(7-position%8))&1); ++position; }
        return result;
    }
    bool Skip(uint64_t count) {
        if(count>size*8-position) return ok=false;
        position+=size_t(count); return ok;
    }
    bool Unary(unsigned maximum=0xffffffffu) {
        unsigned zeros=0;
        while(ok && !Read(1)) if(++zeros>=maximum) return false;
        return ok;
    }
};
bool Residual(Bits& bits,unsigned block,unsigned order) {
    unsigned method=unsigned(bits.Read(2)), partitions=unsigned(bits.Read(4));
    if(method>1 || !bits.ok || (block&((1u<<partitions)-1))) return false;
    unsigned samples=block>>partitions;
    if(samples<order) return false;
    unsigned parameter_bits=4+method;
    for(unsigned p=0;p<(1u<<partitions);++p) {
        unsigned count=samples-(p==0?order:0), parameter=unsigned(bits.Read(parameter_bits));
        if(parameter==(1u<<parameter_bits)-1) {
            unsigned width=unsigned(bits.Read(5));
            if(!bits.Skip(uint64_t(count)*width)) return false;
        } else for(unsigned i=0;i<count;i++) {
            if(!bits.Unary() || !bits.Skip(parameter)) return false;
        }
    }
    return bits.ok;
}
bool Subframe(Bits& bits,unsigned block,unsigned depth) {
    if(bits.Read(1)) return false;
    unsigned type=unsigned(bits.Read(6)); bool wasted=bits.Read(1)!=0;
    if(wasted) {
        unsigned count=1;
        while(bits.ok && !bits.Read(1)) if(++count>=depth) return false;
        if(count>=depth) return false;
        depth-=count;
    }
    if(!bits.ok) return false;
    if(type==0) return bits.Skip(depth);
    if(type==1) return bits.Skip(uint64_t(block)*depth);
    unsigned order=0;
    if(type>=8 && type<=12) order=type-8;
    else if(type>=32) order=type-31;
    else return false;
    if(order>block || !bits.Skip(uint64_t(order)*depth)) return false;
    if(type>=32) {
        unsigned precision=unsigned(bits.Read(4))+1;
        if(precision==16 || !bits.Skip(5+uint64_t(order)*precision)) return false;
    }
    return Residual(bits,block,order);
}
// RFC 9639 sections 9.1-9.3: walk each subframe, not a sync-byte scan.
bool FlacFrame(const uint8_t* b,size_t size,const Quality& quality,unsigned max_block,
               uint64_t expected_sample,uint64_t frame_number,size_t& consumed,unsigned& block) {
    Bits bits{b,size};
    if(bits.Read(14)!=0x3ffe || bits.Read(1)) return false;
    bool variable=bits.Read(1)!=0;
    unsigned block_code=unsigned(bits.Read(4)),rate_code=unsigned(bits.Read(4));
    unsigned channels=unsigned(bits.Read(4)),depth_code=unsigned(bits.Read(3));
    if(bits.Read(1) || !block_code || rate_code==15 || channels>10 || depth_code==3) return false;
    unsigned first=unsigned(bits.Read(8)),continuations=0; uint64_t number=0;
    if(first<128) number=first;
    else {
        unsigned mask=128;
        while(first&mask) { ++continuations; mask>>=1; }
        if(continuations<2 || continuations>7 || (!variable && continuations>6)) return false;
        number=first&(mask-1);
        for(unsigned i=1;i<continuations;i++) {
            unsigned next=unsigned(bits.Read(8)); if((next&192)!=128) return false;
            number=(number<<6)|(next&63);
        }
        static const uint64_t minimum[]={0,0,0x80,0x800,0x10000,0x200000,0x4000000,0x80000000};
        if(number<minimum[continuations]) return false;
    }
    if(number!=(variable?expected_sample:frame_number)) return false;
    if(block_code==1) block=192;
    else if(block_code<=5) block=576u<<(block_code-2);
    else if(block_code==6) block=unsigned(bits.Read(8))+1;
    else if(block_code==7) block=unsigned(bits.Read(16))+1;
    else block=256u<<(block_code-8);
    static const unsigned rates[]={0,88200,176400,192000,8000,16000,22050,24000,32000,44100,48000,96000};
    unsigned rate=quality.rate;
    if(rate_code && rate_code<12) rate=rates[rate_code];
    if(rate_code==12) rate=unsigned(bits.Read(8))*1000;
    if(rate_code==13) rate=unsigned(bits.Read(16));
    if(rate_code==14) rate=unsigned(bits.Read(16))*10;
    static const unsigned depths[]={0,8,12,0,16,20,24,32};
    unsigned depth=depth_code?depths[depth_code]:quality.bits;
    if(rate!=quality.rate || depth!=quality.bits || (channels<8?channels+1:2)!=quality.channels || block>max_block) return false;
    bits.Read(8);
    if(!bits.ok || Crc8(b,bits.position/8)) return false;
    for(unsigned c=0;c<quality.channels;c++) {
        unsigned channel_depth=depth+((channels==8&&c==1)||(channels==9&&c==0)||(channels==10&&c==1));
        if(!Subframe(bits,block,channel_depth)) return false;
    }
    if(bits.position%8 && bits.Read(unsigned(8-bits.position%8))!=0) return false;
    bits.Read(16);
    if(!bits.ok || Crc16(b,bits.position/8)) return false;
    consumed=bits.position/8; return true;
}
bool Flac(const uint8_t* b,size_t size,RecordingFile& out) {
    if(size<42 || (b[4]!=0 && b[4]!=128) || b[5] || b[6] || b[7]!=34) return false;
    uint64_t packed=0; for(unsigned i=18;i<26;i++) packed=(packed<<8)|b[i];
    out.quality={Codec::Flac,unsigned(packed>>44),unsigned((packed>>41)&7)+1,unsigned((packed>>36)&31)+1,0};
    uint64_t total=packed&0xfffffffffULL;
    unsigned maximum=(unsigned(b[10])<<8)|b[11];
    if(!out.quality.rate || out.quality.bits<4 || maximum<16) return false;
    size_t p=4; bool last=false,comments=false;
    do {
        if(size-p<4) return false;
        unsigned type=b[p]&127; last=(b[p]&128)!=0;
        size_t length=(size_t(b[p+1])<<16)|(size_t(b[p+2])<<8)|b[p+3]; p+=4;
        if(type==127 || length>size-p || p+length>metadata_limit || (type==0&&p!=8)) return false;
        if(type==6)Picture(b+p,length,out);
        if(type==4) {
            if(comments || !Comments(b+p,length,out,false)) return false;
            comments=true;
        }
        p+=length;
    } while(!last);
    uint64_t samples=0,frames=0;
    while(p<size) {
        size_t consumed=0; unsigned block=0;
        if(!FlacFrame(b+p,size-p,out.quality,maximum,samples,frames,consumed,block)) return true;
        samples+=block; ++frames; p+=consumed;
        if(samples>total || (samples<total && block!=maximum && !(b[p-consumed+1]&1))) return true;
    }
    out.complete=frames && total && samples==total;
    return true;
}
bool Ogg(const uint8_t* b,size_t size,RecordingFile& out) {
    Stream stream; size_t p=0; unsigned packets=0;
    std::vector<uint8_t> packet; bool packet_open=false;
    while(p<size) {
        if(size-p<27) return false;
        size_t header=27+b[p+26]; if(header>size-p) return false;
        size_t body=0; for(size_t i=27;i<header;i++) body+=b[p+i];
        if(header+body>size-p || stream.complete) return true;
        if(p==0 && (b[p+5]!=2 || Little(b+p+18)!=0 || Little(b+p+6)!=0 || Little(b+p+10)!=0)) return true;
        auto state=stream.Push(b+p,header+body);
        if(state==Result::Invalid || state==Result::Ignore || (p!=0 && state==Result::Begin)) return true;
        out.quality={Codec::Vorbis,stream.rate,stream.channels,0,stream.bitrate};
        if(bool(b[p+5]&1)!=packet_open) return true;
        size_t at=p+header;
        for(size_t i=p+27;i<p+header;i++) {
            unsigned length=b[i];
            if(packets<3) {
                if(packet.size()+length>metadata_limit) return false;
                packet.insert(packet.end(),b+at,b+at+length);
            }
            at+=length; packet_open=length==255;
            if(packet_open) continue;
            if(packets<3) {
                const unsigned kind[]={1,3,5};
                if(packet.size()<7 || packet[0]!=kind[packets] || std::memcmp(packet.data()+1,"vorbis",6)) return true;
                if(packets==1 && !Comments(packet.data()+7,packet.size()-7,out,true)) return false;
            }
            ++packets; packet.clear();
        }
        p+=header+body;
    }
    out.complete=stream.complete && packets>=4 && !packet_open && stream.samples>0;
    return true;
}
std::string Utf8(const std::wstring& value) {
    std::string result;
    for(size_t i=0;i<value.size();i++) {
        uint32_t c=uint32_t(value[i]);
        if(c>=0xd800 && c<=0xdbff && i+1<value.size()) {
            uint32_t low=uint32_t(value[i+1]);
            if(low>=0xdc00 && low<=0xdfff) { c=0x10000+((c-0xd800)<<10)+(low-0xdc00); ++i; }
        }
        if(c<128) result+=char(c);
        else if(c<2048) { result+=char(192|(c>>6)); result+=char(128|(c&63)); }
        else if(c<65536) { result+=char(224|(c>>12)); result+=char(128|((c>>6)&63)); result+=char(128|(c&63)); }
        else { result+=char(240|(c>>18)); result+=char(128|((c>>12)&63)); result+=char(128|((c>>6)&63)); result+=char(128|(c&63)); }
    }
    return result;
}
}
bool ReadRecordingArtwork(const std::vector<uint8_t>& bytes,RecordingArtwork& out) {
    out={};ImageInfo info;if(!ReadImageInfo(bytes,info))return false;
    out.valid=true;out.width=info.width;out.height=info.height;out.bytes=bytes.size();out.mime=info.mime;
    out.digest={14695981039346656037ULL,0x9e3779b97f4a7c15ULL};
    for(unsigned char byte:bytes) {
        out.digest[0]^=byte;out.digest[0]*=1099511628211ULL;
        out.digest[1]^=uint64_t(byte)+0x9e3779b97f4a7c15ULL+(out.digest[1]<<6)+(out.digest[1]>>2);
    }
    return true;
}
bool InspectRecording(const uint8_t* bytes,size_t size,RecordingFile& out) {
    out={};
    if(!bytes || size<4 || size>512u*1024*1024) return false;
    bool valid=false;
    if(!std::memcmp(bytes,"fLaC",4)) valid=Flac(bytes,size,out);
    else if(!std::memcmp(bytes,"OggS",4)) valid=Ogg(bytes,size,out);
    out.owned=valid && Field(out.tags,"HISTORY_COMPLETE_LISTEN")=="1" &&
        (Field(out.tags,"HISTORY_CAPTURE")=="native compressed FLAC input" || Field(out.tags,"HISTORY_CAPTURE")=="native compressed Ogg pages");
    return valid;
}
bool MatchesRecording(const RecordingFile& file,const Catalog& c) {
    auto uri=Field(file.tags,"SPOTIFY_URI");
    if(!uri.empty() && !c.spotify_uri.empty()) return uri==c.spotify_uri;
    if(c.title.empty() || c.artist.empty() || c.album.empty()) return false;
    if(Field(file.tags,"TITLE")!=Utf8(c.title) || Field(file.tags,"ALBUM")!=Utf8(c.album)) return false;
    auto artist=Field(file.tags,"ALBUMARTIST");
    if(artist.empty()) artist=Field(file.tags,"ARTIST");
    if(artist!=Utf8(c.artist)) return false;
    auto number=[&](const char* field,unsigned expected) {
        auto text=Field(file.tags,field);
        if(text.empty()) return expected<=1;
        return text==std::to_string(expected) || (expected==0 && text=="1");
    };
    if(!number("TRACKNUMBER",c.track) || !number("DISCNUMBER",c.disc)) return false;
    auto duration=Field(file.tags,"HISTORY_MEDIA_DURATION");
    if(c.duration<=0 || duration.empty()) return false;
    char* end=nullptr; double seconds=std::strtod(duration.c_str(),&end);
    return end && !*end && std::isfinite(seconds) && std::fabs(seconds-c.duration)<=0.5;
}
std::wstring RecordingSuffix(const Catalog& c) {
    std::string identity=c.spotify_uri;
    if(identity.empty()) identity=Utf8(c.artist)+'\0'+Utf8(c.album)+'\0'+Utf8(c.title)+'\0'+
        std::to_string(c.disc)+":"+std::to_string(c.track)+":"+std::to_string(c.duration);
    uint64_t hash=14695981039346656037ULL;
    for(unsigned char byte:identity) { hash^=byte; hash*=1099511628211ULL; }
    wchar_t suffix[32]; std::swprintf(suffix,32,L" [%016llx]",static_cast<unsigned long long>(hash));
    return suffix;
}
}
