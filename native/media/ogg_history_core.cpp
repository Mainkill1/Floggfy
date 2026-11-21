#include "ogg_history_core.h"
#include <cstring>
namespace history {
static uint64_t le(const uint8_t* p, unsigned n) {
    uint64_t value=0; for(unsigned i=0;i<n;i++) value |= uint64_t(p[i])<<(8*i);
    return value;
}
static uint32_t crc_table[256];
static const bool initialized = [] {
    for(unsigned i=0;i<256;i++) {
        uint32_t r=i<<24;
        for(unsigned bit=0;bit<8;bit++) r=r&0x80000000 ? (r<<1)^0x04c11db7 : r<<1;
        crc_table[i]=r;
    } return true;
}();
bool ParsePageIdentity(const uint8_t* b,size_t n,uint32_t& serial,uint32_t& sequence) {
    if(!b || n<27 || n>65307 || std::memcmp(b,"OggS",4) || b[4] || (b[5]&~7)) return false;
    size_t header=27+b[26]; if(header>n) return false;
    size_t body=0; for(size_t i=27;i<header;i++) body+=b[i];
    if(header+body!=n) return false;
    serial=uint32_t(le(b+14,4));sequence=uint32_t(le(b+18,4));return true;
}
bool ParsePage(const uint8_t* b, size_t n, Page& out) {
    (void)initialized;
    uint32_t serial=0,sequence=0;
    if(!ParsePageIdentity(b,n,serial,sequence))return false;
    const size_t header=27+b[26],body=n-header; // ParsePageIdentity verified the lacing sum.
    uint32_t crc=0;
    for(size_t i=0;i<n;i++) {
        uint8_t v = i>=22 && i<26 ? 0 : b[i];
        crc=(crc<<8)^crc_table[((crc>>24)^v)&255];
    }
    if(crc!=le(b+22,4)) return false;
    out={}; out.flags=b[5]; out.granule=int64_t(le(b+6,8));
    out.serial=serial; out.sequence=sequence;
    if((out.flags&2) && !(out.flags&1) && body>=30 && !std::memcmp(b+header,"\x01vorbis",7)
       && le(b+header+7,4)==0) {
        out.channels=b[header+11]; out.rate=uint32_t(le(b+header+12,4));
        int32_t nominal=int32_t(le(b+header+20,4)); out.bitrate=nominal>0 ? uint32_t(nominal) : 0;
        out.vorbis_start=out.channels>0 && out.rate>=8000 && out.rate<=384000;
    }
    return true;
}
const char* OggRejectionName(OggRejection reason) noexcept {
    switch(reason) {
        case OggRejection::None:return "none";
        case OggRejection::InvalidPage:return "invalid-page";
        case OggRejection::Inactive:return "inactive-stream";
        case OggRejection::Serial:return "serial-change";
        case OggRejection::Sequence:return "sequence-discontinuity";
        case OggRejection::Bos:return "unexpected-bos";
        case OggRejection::Granule:return "invalid-granule";
        case OggRejection::GranuleRegression:return "granule-regression";
        case OggRejection::EosGranule:return "invalid-eos-granule";
    }
    return "unknown";
}
Result Stream::Push(const uint8_t* b, size_t n) {
    Page p;
    const auto invalid=[&](OggRejection reason) {
        rejection=reason;active=false;complete=false;return Result::Invalid;
    };
    if(!ParsePage(b,n,p))return invalid(OggRejection::InvalidPage);
    if(p.vorbis_start) {
        *this={}; active=true; serial=p.serial; next=p.sequence+1;
        rate=p.rate; channels=p.channels; bitrate=p.bitrate; pages=1; samples=p.granule;
        return Result::Begin;
    }
    if(!active){rejection=OggRejection::Inactive;return Result::Ignore;}
    if(p.serial!=serial)return invalid(OggRejection::Serial);
    if(p.sequence!=next)return invalid(OggRejection::Sequence);
    if(p.flags&2)return invalid(OggRejection::Bos);
    if(p.granule < -1)return invalid(OggRejection::Granule);
    if(p.granule>=0 && samples>=0 && p.granule<samples)return invalid(OggRejection::GranuleRegression);
    ++next; ++pages; if(p.granule>=0) samples=p.granule;
    if(p.flags&4) {
        active=false; complete=samples>0 && p.granule>0;
        return complete ? Result::Complete : invalid(OggRejection::EosGranule);
    }
    return Result::Append;
}
}
