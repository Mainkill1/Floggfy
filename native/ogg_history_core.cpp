#include "ogg_history_core.h"
#include <cstring>
#include <cmath>
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
bool ParsePage(const uint8_t* b, size_t n, Page& out) {
    (void)initialized;
    if(!b || n<27 || n>65307 || std::memcmp(b,"OggS",4) || b[4] || (b[5]&~7)) return false;
    size_t header=27+b[26]; if(header>n) return false;
    size_t body=0; for(size_t i=27;i<header;i++) body+=b[i];
    if(header+body!=n) return false;
    uint32_t crc=0;
    for(size_t i=0;i<n;i++) {
        uint8_t v = i>=22 && i<26 ? 0 : b[i];
        crc=(crc<<8)^crc_table[((crc>>24)^v)&255];
    }
    if(crc!=le(b+22,4)) return false;
    out={}; out.flags=b[5]; out.granule=int64_t(le(b+6,8));
    out.serial=uint32_t(le(b+14,4)); out.sequence=uint32_t(le(b+18,4));
    if((out.flags&2) && !(out.flags&1) && body>=30 && !std::memcmp(b+header,"\x01vorbis",7)
       && le(b+header+7,4)==0) {
        out.channels=b[header+11]; out.rate=uint32_t(le(b+header+12,4));
        int32_t nominal=int32_t(le(b+header+20,4)); out.bitrate=nominal>0 ? uint32_t(nominal) : 0;
        out.vorbis_start=out.channels>0 && out.rate>=8000 && out.rate<=384000;
    }
    return true;
}
Result Stream::Push(const uint8_t* b, size_t n) {
    Page p;
    if(!ParsePage(b,n,p)) { active=false; complete=false; return Result::Invalid; }
    if(p.vorbis_start) {
        *this={}; active=true; serial=p.serial; next=p.sequence+1;
        rate=p.rate; channels=p.channels; bitrate=p.bitrate; pages=1; samples=p.granule;
        return Result::Begin;
    }
    if(!active) return Result::Ignore;
    if(p.serial!=serial || p.sequence!=next || (p.flags&2) || p.granule < -1
       || (p.granule>=0 && samples>=0 && p.granule<samples)) {
        active=false; complete=false; return Result::Invalid;
    }
    ++next; ++pages; if(p.granule>=0) samples=p.granule;
    if(p.flags&4) {
        active=false; complete=samples>0 && p.granule>0;
        return complete ? Result::Complete : Result::Invalid;
    }
    return Result::Append;
}
std::string Listen::Observe(const std::string& key, double pos, double length,
                            bool is_playing, double time) {
    std::string done;
    transient=false;
    double elapsed=time-last_time;
    if(key!=identity) {
        if(eligible && playing && duration>0 && elapsed>=0 && elapsed<=3
           && last_position>=duration-1.0 && last_position+elapsed>=duration-0.1) done=identity;
        identity=key; start_time=time-pos; duration=length;
        eligible=!key.empty() && pos>=0 && pos<=1.5 && length>0;
        identity_time=time; pending_start=!eligible && !key.empty();
    } else if(!key.empty()) {
        // SMTC title and timeline are separate snapshots. At a natural end,
        // the next timeline can arrive before its title. Keep the old end
        // position briefly, so that an inconsistent snapshot cannot erase
        // already observed coverage or replace the previous track's metadata.
        if(eligible && playing && last_position>=duration-1.0 && pos<=1.5 &&
           elapsed>=0 && elapsed<=3) { transient=true; return {}; }
        if(pending_start && time-identity_time<=2 && pos>=0 && pos<=1.5 && length>0) {
            start_time=time-pos; duration=length; eligible=true; pending_start=false;
            last_time=time; last_position=pos; playing=is_playing; return {};
        }
        double expected=last_position+(playing ? elapsed : 0);
        if(elapsed<0 || elapsed>3 || pos<last_position-1.5 || pos>expected+1.5
           || std::fabs(length-duration)>1.0) eligible=false;
        if(eligible && playing && !is_playing && pos>=duration && expected>=duration-0.1) {
            done=identity; eligible=false; pending_start=false;
        }
    }
    last_time=time; last_position=pos; playing=is_playing;
    return done;
}
}
