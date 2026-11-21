#include "../tooling/base64.h"
#include "../tooling/binary.h"
#include "recording_file.h"
#include "../tooling/fnv1a.h"
#include "mp3_stream.h"
#include "ogg_history_core.h"
#include "image_info.h"
#include "vorbis_headers.h"
#include "vorbis_comments.h"
#include "aac_stream.h"
#include "compressed_buffer.h"
#include "flac_history_core.h"
#include "recording_formats.h"
#include "recording_limits.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace history {
namespace {
constexpr size_t metadata_limit=16*1024*1024;
const std::string& Field(const Tags& tags,const char* name) {
    for(const auto& field:tags.fields) if(field.first==name) return field.second;
    static const std::string empty;
    return empty;
}
bool MatchesUnsignedField(const Tags& tags,const char* name,uint64_t expected) {
    const auto& value=Field(tags,name);
    const auto end=value.data()+value.size();
    uint64_t number=0;
    const auto parsed=std::from_chars(value.data(),end,number);
    return parsed.ec==std::errc{} && parsed.ptr==end && number && number==expected;
}
void Picture(const uint8_t* bytes,size_t size,RecordingFile& record) {
    record.has_artwork=true;
    // Only front covers are rewritten. Unknown layouts and other picture
    // types remain opaque so an album-art refresh cannot discard a booklet.
    if(size<4 || binary::ReadBig32(bytes)!=3){record.artwork_edit_safe=false;return;}
    size_t at=4;
    if(size-at<4)return;
    const auto mime_size=binary::ReadBig32(bytes+at);at+=4;if(mime_size>size-at)return;
    const bool png=mime_size==9 && !memcmp(bytes+at,"image/png",9);
    const bool jpeg=mime_size==10 && !memcmp(bytes+at,"image/jpeg",10);at+=mime_size;
    if(!png && !jpeg){record.artwork_edit_safe=false;return;}
    if(size-at<4)return;
    const auto description_size=binary::ReadBig32(bytes+at);at+=4;if(description_size>size-at)return;at+=description_size;
    if(size-at<20)return;
    at+=16;const auto length=binary::ReadBig32(bytes+at);at+=4;
    if(length!=size-at)return;
    if(length>4*1024*1024){record.artwork_edit_safe=false;return;}
    // A bad/missing signature is confirmed corrupt for its declared format.
    // A recognizable but unsupported/bounds-rejected image is opaque: never
    // mistake an inspection limit for permission to replace a good cover.
    if((png && (length<8 || memcmp(bytes+at,"\x89PNG\r\n\x1a\n",8))) ||
       (jpeg && (length<2 || bytes[at]!=0xff || bytes[at+1]!=0xd8)))return;
    RecordingArtwork image;std::vector<uint8_t> encoded(bytes+at,bytes+at+length);
    if(!ReadRecordingArtwork(encoded,image)){record.artwork_edit_safe=false;return;}
    record.artworks.push_back(image);
    if(!record.artwork.valid || uint64_t(image.width)*image.height>uint64_t(record.artwork.width)*record.artwork.height) {
        // Keep the best validated image (at most 4 MiB) for the shared upgrade
        // planner; raw opaque pictures are preserved independently by callers.
        record.tags.cover=std::move(encoded);record.tags.mime=image.mime;
        record.artwork=std::move(image);
    }
}
bool Comments(const uint8_t* b,size_t size,RecordingFile& record,bool vorbis) {
    if(size>metadata_limit || (vorbis && (!size || b[size-1]!=1)))return false;
    std::string_view vendor;
    return ReadVorbisComments(b,size-size_t(vorbis),vendor,[&](std::string_view value) {
        const auto equals=value.find('=');
        if(equals==value.npos)return;
        auto key=CommentKey(value.substr(0,equals));
        // Artwork can dwarf all textual metadata and is irrelevant to identity.
        if(key=="METADATA_BLOCK_PICTURE") {
            const auto encoded=value.substr(equals+1);
            record.picture_comments.emplace_back(encoded);
            if(!encoded.empty()) {
                record.has_artwork=true;std::vector<uint8_t> picture;
                if(base64::DecodePadded(encoded,metadata_limit,picture,true) && !picture.empty())
                    Picture(picture.data(),picture.size(),record);
                else record.artwork_edit_safe=false;
            }
        } else record.tags.fields.emplace_back(std::move(key),value.substr(equals+1));
    });
}
struct BufferedBytes {
    const CompressedBuffer& bytes;size_t base;
    uint8_t operator[](size_t at) const noexcept {return bytes[base+at];}
};
template<class Source> uint8_t Crc8(const Source& b,size_t n) {
    uint8_t crc=0;
    for(size_t i=0;i<n;++i) { crc^=b[i]; for(unsigned j=0;j<8;j++) crc=uint8_t((crc<<1)^((crc&128)?7:0)); }
    return crc;
}
template<class Source> uint16_t Crc16(const Source& b,size_t n) {
    uint16_t crc=0;
    for(size_t i=0;i<n;++i) { crc^=uint16_t(b[i])<<8; for(unsigned j=0;j<8;j++) crc=uint16_t((crc<<1)^((crc&32768)?0x8005:0)); }
    return crc;
}
template<class Source> struct Bits {
    const Source& bytes; size_t size,position=0; bool ok=true;
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
template<class Source> bool Residual(Bits<Source>& bits,unsigned block,unsigned order) {
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
template<class Source> bool Subframe(Bits<Source>& bits,unsigned block,unsigned depth) {
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
struct FlacFrameInfo {
    bool header_valid=false,variable=false;
    uint64_t number=0;
    unsigned block=0;
    size_t length=0;
};
// RFC 9639 sections 9.1-9.3: walk each subframe, not a sync-byte scan.
template<class Source> bool FlacFrame(const Source& b,size_t size,const Quality& quality,unsigned max_block,
               uint64_t expected_sample,uint64_t frame_number,FlacFrameInfo& frame,
               bool check_number=true,bool* need_more=nullptr) {
    Bits<Source> bits{b,size};
    struct IncompleteResult {
        const Bits<Source>& bits;bool* result;
        ~IncompleteResult(){if(result)*result=!bits.ok;}
    } incomplete{bits,need_more};
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
    if(check_number && number!=(variable?expected_sample:frame_number)) return false;
    unsigned block=0;
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
    frame={true,variable,number,block};
    for(unsigned c=0;c<quality.channels;c++) {
        unsigned channel_depth=depth+((channels==8&&c==1)||(channels==9&&c==0)||(channels==10&&c==1));
        if(!Subframe(bits,block,channel_depth)) return false;
    }
    if(bits.position%8 && bits.Read(unsigned(8-bits.position%8))!=0) return false;
    bits.Read(16);
    if(!bits.ok || Crc16(b,bits.position/8)) return false;
    frame.length=bits.position/8; return true;
}
bool FitsFlacCoverage(const FlacFrameInfo& frame,unsigned maximum,uint64_t remaining) {
    return frame.block<=remaining && (frame.block==remaining || frame.block==maximum || frame.variable);
}
bool Flac(const uint8_t* b,size_t size,RecordingFile& out) {
    if(size<42 || (binary::ReadBig32(b+4)&0x7fffffff)!=34) return false;
    const auto packed=binary::ReadBig64(b+18);
    out.quality={Codec::Flac,unsigned(packed>>44),unsigned((packed>>41)&7)+1,unsigned((packed>>36)&31)+1,0};
    uint64_t total=packed&0xfffffffffULL;
    const auto maximum=binary::ReadBig16(b+10);
    if(!out.quality.rate || out.quality.bits<4 || maximum<16) return false;
    size_t p=42;
    bool last=(b[4]&128)!=0,comments=false;
    while(!last) {
        if(size-p<4) return false;
        unsigned type=b[p]&127; last=(b[p]&128)!=0;
        size_t length=(size_t(b[p+1])<<16)|(size_t(b[p+2])<<8)|b[p+3]; p+=4;
        if(type==0 || type==127 || length>size-p || p+length>metadata_limit) return false;
        if(type==6)Picture(b+p,length,out);
        if(type==4) {
            if(comments || !Comments(b+p,length,out,false)) return false;
            comments=true;
        }
        p+=length;
    }
    uint64_t samples=0,frames=0;
    while(p<size) {
        FlacFrameInfo frame;
        if(!FlacFrame(b+p,size-p,out.quality,maximum,samples,frames,frame) ||
           !FitsFlacCoverage(frame,maximum,total-samples)) return true;
        samples+=frame.block; ++frames; p+=frame.length;
    }
    out.complete=frames && total && samples==total;
    return true;
}
bool Ogg(const uint8_t* b,size_t size,RecordingFile& out) {
    Stream stream; size_t p=0; unsigned packets=0;
    std::vector<uint8_t> packet,identification; bool packet_open=false,headers_valid=false;
    while(p<size) {
        if(size-p<27) return false;
        size_t header=27+b[p+26]; if(header>size-p) return false;
        size_t body=0; for(size_t i=27;i<header;i++) body+=b[p+i];
        if(header+body>size-p || stream.complete) return true;
        if(p==0 && (b[p+5]!=2 || binary::ReadLittle32(b+p+6)!=0 || binary::ReadLittle32(b+p+10)!=0)) return true;
        auto state=stream.Push(b+p,header+body);
        // libogg uses -1 as an uninitialized page number. Standalone files
        // crossing that value are not reliably readable without repaging.
        if(state==Result::Invalid || state==Result::Ignore || (p!=0 && state==Result::Begin) || !stream.next) return true;
        out.quality={Codec::Vorbis,stream.rate,stream.channels,0,stream.bitrate};
        if(bool(b[p+5]&1)!=packet_open) return true;
        size_t at=p+header;
        for(size_t i=p+27;i<p+header;i++) {
            unsigned length=b[i];
            if(packets<3) {
                const size_t limit=packets==0?30:packets==2?1024*1024:metadata_limit;
                if(packet.size()+length>limit) return false;
                packet.insert(packet.end(),b+at,b+at+length);
            }
            at+=length; packet_open=length==255;
            if(packet_open) continue;
            if(packets<3) {
                const unsigned kind[]={1,3,5};
                if(packet.size()<7 || packet[0]!=kind[packets] || std::memcmp(packet.data()+1,"vorbis",6)) return false;
                if(packets==0)identification=packet;
                if(packets==1 && !Comments(packet.data()+7,packet.size()-7,out,true)) return false;
                if(packets==2) {
                    // Unsupported or invalid headers must not confer ownership: an
                    // existing recording outside these limits is not repairable.
                    const auto result=ValidateVorbisHeaders(identification,packet);
                    if(result!=VorbisHeadersResult::Valid) {
                        out.inspection_resource_failure=result==VorbisHeadersResult::ResourceFailure;
                        return false;
                    }
                    headers_valid=true;
                }
            }
            ++packets; packet.clear();
        }
        p+=header+body;
    }
    out.complete=headers_valid && stream.complete && packets>=4 && !packet_open && stream.samples>0;
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
void FlacBufferCoverage::Scan(const CompressedBuffer& bytes,const FlacInfo& info,size_t audio_offset) {
    complete=false;
    if(!next_offset)next_offset=audio_offset;
    if(next_offset>bytes.size() || !info.total_samples || samples>info.total_samples)return;
    const Quality quality{Codec::Flac,info.rate,info.channels,info.bits,0};
    // Bound an unfinished/corrupt frame independently of the growing track.
    // STREAMINFO's frame-size fields have 24 bits; streams exceeding this
    // conservative inspection limit never acquire proof of completion.
    constexpr size_t maximum_frame_bytes=0xffffff;
    while(next_offset<bytes.size() && samples<info.total_samples) {
        FlacFrameInfo frame;
        if(!FlacFrame(BufferedBytes{bytes,next_offset},std::min(bytes.size()-next_offset,maximum_frame_bytes),
                      quality,info.max_block,samples,frames,frame) ||
           !FitsFlacCoverage(frame,info.max_block,info.total_samples-samples))return;
        samples+=frame.block;++frames;next_offset+=frame.length;
    }
    complete=frames && samples==info.total_samples && next_offset==bytes.size();
}
size_t FlacCaptureReader::MemoryUsage() const noexcept {
    return index.capacity()*sizeof(Frame)+pending.capacity()*sizeof(Fragment)+
           resync_pending.capacity()*sizeof(size_t);
}
void FlacCaptureReader::Scan(CompressedBuffer& bytes,const FlacInfo& info,size_t audio_offset,size_t available) {
    complete=false;
    if(failed)return;
    if(bytes.size()<audio_offset){reason="waiting_metadata";return;}
    if(!next_offset)next_offset=audio_offset;
    if(next_offset>bytes.size() || !info.total_samples || samples>info.total_samples) {
        reason="invalid_capture_bounds";failed=true;return;
    }
    incoming_sample_known=false;
    constexpr size_t frame_limit=0xffffff,resync_limit=1024*1024,index_limit=8*1024*1024;
    const Quality quality{Codec::Flac,info.rate,info.channels,info.bits,0};
    auto parse=[&](size_t at,FlacFrameInfo& frame,bool* need_more=nullptr) {
        return FlacFrame(BufferedBytes{bytes,at},std::min(bytes.size()-at,frame_limit),
                         quality,info.max_block,0,0,frame,false,need_more);
    };
    auto observe=[&](const FlacFrameInfo& frame) {
        incoming_sample_known=frame.header_valid;
        if(frame.header_valid)incoming_sample=frame.variable?frame.number:frame.number*info.max_block;
    };
    auto equal=[&](size_t left,size_t right,size_t length) {
        for(size_t i=0;i<length;++i)if(bytes[left+i]!=bytes[right+i])return false;
        return true;
    };
    auto grow=[&](auto& values,bool required=true) {
        if(values.size()<values.capacity())return true;
        const auto old=values.capacity()*sizeof(values[0]);
        const auto count=std::max(size_t(16),values.capacity()*2);
        const auto needed=count*sizeof(values[0]);
        // Include the temporary old+new allocation during vector growth.
        if(needed>available || needed-old>index_limit-MemoryUsage()) {
            if(required){reason="index_memory_limit";failed=true;}
            return false;
        }
        try {values.reserve(count);} catch(const std::bad_alloc&) {
            if(required){reason="index_allocation_failed";failed=true;}
            return false;
        }
        available-=needed-old;return true;
    };
    auto is_before=[&](const FlacFrameInfo& h) {
        return h.header_valid && (h.variable?h.number<samples:h.number<frames);
    };
    auto is_next=[&](const FlacFrameInfo& h) {
        return h.header_valid && h.number==(h.variable?samples:frames);
    };
    auto find_resync=[&](const auto& accepted)->size_t {
        if(resync_origin!=next_offset) {
            resync_origin=next_offset;resync_next=next_offset+1;resync_pending.clear();
        }
        // A split header/frame can become valid after an append. Complete
        // rejected candidates cannot, so retain only the unfinished ones.
        size_t kept=0;
        for(const auto at:resync_pending) {
            FlacFrameInfo candidate;bool need_more=false;
            ++resync_candidate_parses;
            if(parse(at,candidate,&need_more)&&accepted(candidate))return at;
            if(need_more)resync_pending[kept++]=at;
        }
        resync_pending.resize(kept);
        const auto end=next_offset+std::min(resync_limit,bytes.size()-next_offset);
        for(;resync_next+2<=end;++resync_next) {
            const auto at=resync_next;++resync_bytes_examined;
            if(bytes[at]!=0xff||(bytes[at+1]&0xfe)!=0xf8)continue;
            FlacFrameInfo candidate;bool need_more=false;
            ++resync_candidate_parses;
            if(parse(at,candidate,&need_more)&&accepted(candidate))return at;
            if(need_more) {
                // Speculation must not invalidate an ordinary partial frame
                // when no recovery memory is available. Keep this cursor for
                // the next append; the strict frame parser can still finish.
                if(!grow(resync_pending,false))return 0;
                resync_pending.push_back(at);
            }
        }
        return 0;
    };
    // A fallback is activated only by a checksum-valid backward frame.
    // Normal partial callbacks retain the strict cursor; speculative recovery
    // candidates are optional and never prevent that frame from completing.
    if(!recovering) {
        FlacBufferCoverage strict{next_offset,samples,frames,false};
        strict.Scan(bytes,info,audio_offset);
        next_offset=strict.next_offset;samples=strict.samples;frames=strict.frames;
        if(strict.complete){
            std::vector<size_t>().swap(resync_pending);resync_origin=~size_t(0);
            complete=true;reason="complete";return;
        }
        if(next_offset==bytes.size()){reason="waiting_frame";return;}
        FlacFrameInfo frame;
        const bool valid=parse(next_offset,frame);observe(frame);
        if(valid && !is_before(frame)) {reason="sample_gap_or_invalid_block";failed=true;return;}
        const bool backward=valid || find_resync(is_before)!=0;
        if(failed)return;
        if(!backward) {reason=bytes.size()-next_offset>resync_limit?"resync_limit":"waiting_frame";return;}
        uint64_t sample=0;
        for(size_t at=audio_offset;at<next_offset;) {
            FlacFrameInfo prior;
            if(!parse(at,prior)||!grow(index)){if(!failed){reason="invalid_validated_prefix";failed=true;}return;}
            index.push_back({at,prior.length,sample,prior.block});sample+=prior.block;at+=prior.length;
        }
        recovering=true;resync_origin=~size_t(0);resync_pending.clear();
    }
    auto frame_index=[&](const FlacFrameInfo& h)->size_t {
        if(!h.variable)return h.number<=index.size()?size_t(h.number):index.size()+1;
        const auto it=std::lower_bound(index.begin(),index.end(),h.number,
                                     [](const Frame& f,uint64_t sample){return f.sample<sample;});
        if(it!=index.end() && it->sample==h.number)return size_t(it-index.begin());
        return h.number==samples?index.size():index.size()+1;
    };
    while(next_offset<bytes.size()) {
        FlacFrameInfo frame;
        const bool valid=parse(next_offset,frame);observe(frame);
        if(valid) {
            const auto i=frame_index(frame);
            if(i<index.size()) {
                const auto& known=index[i];
                if(frame.length!=known.length || !equal(next_offset,known.offset,frame.length)) {
                    reason="conflicting_duplicate";failed=true;return;
                }
                ++duplicate_frames;duplicate_bytes+=frame.length;next_offset+=frame.length;continue;
            }
            if(i!=index.size() || !is_next(frame) ||
               !FitsFlacCoverage(frame,info.max_block,info.total_samples-samples)) {
                reason="sample_gap_or_invalid_block";failed=true;return;
            }
            for(const auto& fragment:pending) {
                if(fragment.index!=i || fragment.length>=frame.length ||
                   !equal(fragment.offset,next_offset,fragment.length)) {
                    reason="conflicting_interrupted_frame";failed=true;return;
                }
            }
            if(!grow(index))return;
            index.push_back({next_offset,frame.length,samples,frame.block});pending.clear();
            samples+=frame.block;++frames;next_offset+=frame.length;continue;
        }
        // The interrupted range must have a valid header for an already
        // known frame or exactly the next frame. Its bytes must later match
        // that frame's prefix; arbitrary corrupt bytes are never discarded.
        if(!frame.header_valid){reason="waiting_or_invalid_frame_header";return;}
        const auto interrupted_index=frame_index(frame);
        if(interrupted_index>index.size()) {reason="sample_gap";failed=true;return;}
        const auto resumed=find_resync([&](const FlacFrameInfo& h){return frame_index(h)<index.size();});
        if(failed)return;
        if(!resumed){reason=bytes.size()-next_offset>resync_limit?"resync_limit":"waiting_interrupted_frame";return;}
        const auto fragment_size=resumed-next_offset;
        if(interrupted_index<index.size()) {
            const auto& known=index[interrupted_index];
            if(fragment_size>=known.length || !equal(next_offset,known.offset,fragment_size)) {
                reason="conflicting_interrupted_frame";failed=true;return;
            }
        } else {
            if(pending.size()>=4096) {reason="fragment_index_limit";failed=true;return;}
            if(!grow(pending))return;
            pending.push_back({next_offset,fragment_size,interrupted_index});
        }
        interrupted_bytes+=fragment_size;next_offset=resumed;
    }
    if(samples!=info.total_samples || !pending.empty()){reason="waiting_frame";return;}
    // Every source range is forward ordered; copying toward the beginning
    // cannot overwrite a later range. Release tail blocks after all copies.
    size_t destination=audio_offset;
    for(const auto& frame:index) {
        if(!bytes.CopyRange(destination,frame.offset,frame.length)) {reason="invalid_compaction_range";failed=true;return;}
        destination+=frame.length;
    }
    bytes.RetainPrefixAndSuffix(destination,bytes.size());next_offset=destination;
    std::vector<Frame>().swap(index);std::vector<Fragment>().swap(pending);recovering=false;
    std::vector<size_t>().swap(resync_pending);resync_origin=~size_t(0);
    complete=true;reason="complete_recovered";
}
bool FindValidatedFlacFrame(const CompressedBuffer& bytes,const Quality& quality,
                            unsigned max_block,uint64_t expected_sample,
                            uint64_t frame_number,size_t begin,size_t& offset) {
    if(begin>=bytes.size())return false;
    bool found=false;
    for(size_t at=begin;at+2<=bytes.size();++at) {
        if(bytes[at]!=0xff||(bytes[at+1]&0xfe)!=0xf8)continue;
        FlacFrameInfo frame;
        if(!FlacFrame(BufferedBytes{bytes,at},bytes.size()-at,quality,max_block,expected_sample,
                      frame_number,frame))continue;
        offset=at;found=true;
    }
    return found;
}
bool ReadRecordingArtwork(const std::vector<uint8_t>& bytes,RecordingArtwork& out) {
    out={};ImageInfo info;if(!ReadImageInfo(bytes,info))return false;
    out.valid=true;out.width=info.width;out.height=info.height;out.bytes=bytes.size();out.mime=info.mime;
    out.digest={kFnv1aOffset,0x9e3779b97f4a7c15ULL};
    for(unsigned char byte:bytes) {
        out.digest[0]=Fnv1aByte(out.digest[0],byte);
        out.digest[1]^=uint64_t(byte)+0x9e3779b97f4a7c15ULL+(out.digest[1]<<6)+(out.digest[1]>>2);
    }
    return true;
}
bool InspectRecording(const uint8_t* bytes,size_t size,RecordingFile& out) {
    out={};
    if(!bytes || size<4 || size>kMaximumRecordingBytes) return false;
    bool valid=false;
    Mp3Info mp3;
    if(!std::memcmp(bytes,"fLaC",4)) valid=Flac(bytes,size,out);
    else if(!std::memcmp(bytes,"OggS",4)) valid=Ogg(bytes,size,out);
    else if(size>=12 && !std::memcmp(bytes+4,"ftyp",4)) {
        AacFile aac;
        valid=InspectAacM4a(bytes,size,aac);
        if(valid) {
            out.quality={Codec::Aac,aac.info.rate,aac.info.channels,0,0};
            out.tags=std::move(aac.tags);out.complete=aac.complete;
            out.has_artwork=aac.has_artwork;out.artwork_edit_safe=aac.artwork_edit_safe;
            for(const auto& bytes:aac.covers) {
                RecordingArtwork image;
                if(!ReadRecordingArtwork(bytes,image)){out.artwork_edit_safe=false;continue;}
                out.artworks.push_back(image);
                if(!out.artwork.valid || uint64_t(image.width)*image.height>uint64_t(out.artwork.width)*out.artwork.height)
                    out.artwork=std::move(image);
            }
        }
    } else if(!std::memcmp(bytes,"ID3",3)||(bytes[0]==0xff&&(bytes[1]&0xe0)==0xe0)) {
        std::string error;
        const auto inspected=InspectOwnedMp3Result(bytes,size,mp3,out.tags,error);
        valid=inspected==Mp3InspectionResult::Valid;
        if(inspected==Mp3InspectionResult::ResourceFailure)out.inspection_resource_failure=true;
        if(valid) {
            out.quality={Codec::Mp3,mp3.rate,mp3.channels,0,
                mp3.Duration()>0?unsigned((mp3.audio_size*8)/mp3.Duration()):0};
            out.complete=mp3.complete;
            if(!out.tags.cover.empty()) {
                out.has_artwork=ReadRecordingArtwork(out.tags.cover,out.artwork);
                out.artwork_edit_safe=out.has_artwork;
                if(out.has_artwork)out.artworks.push_back(out.artwork);
            }
        }
    }
    out.owned=valid && (Field(out.tags,"HISTORY_COMPLETE_STREAM")=="1" || Field(out.tags,"HISTORY_COMPLETE_LISTEN")=="1") &&
        MatchesRecordingCapture(out.quality.codec,Field(out.tags,"HISTORY_CAPTURE"));
    if(out.complete && out.owned && out.quality.codec==Codec::Mp3)
        out.complete=MatchesUnsignedField(out.tags,"HISTORY_SAMPLES",mp3.samples);
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
    const auto hash=Fnv1a64(identity.data(),identity.size());
    wchar_t suffix[32]; std::swprintf(suffix,32,L" [%016llx]",static_cast<unsigned long long>(hash));
    return suffix;
}
}
