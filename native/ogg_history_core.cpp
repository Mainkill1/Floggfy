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
namespace {
static void BeginListen(Listen& listen,const std::string& key,double position,double length,
                        bool playing,double time,double start) {
    listen.identity=key;listen.start_time=start;listen.duration=length;
    listen.eligible=!key.empty() && position>=0 && position<=1.5 && length>0;
    listen.identity_time=time;listen.pending_start=!listen.eligible && !key.empty();
    listen.phase=ListenPhase::Tracking;
    listen.last_time=time;listen.last_position=position;listen.playing=playing;
    if(!key.empty()) ++listen.generation;
}
static std::string CompleteListen(Listen& listen,double finish) {
    listen.completed_generation=listen.generation;
    listen.completed_start=listen.start_time;listen.completed_time=finish;
    listen.eligible=false;listen.pending_start=false;
    return listen.identity;
}
static bool NaturalEnd(const Listen& listen,double elapsed) {
    return listen.eligible && listen.playing && listen.duration>0 && elapsed>=0 && elapsed<=3 &&
           listen.last_position>=listen.duration-1.0 &&
           listen.last_position+elapsed>=listen.duration-0.1;
}
}
std::string Listen::Observe(const std::string& key,double pos,double length,
                            bool is_playing,double time) {
    transient=false;std::string done;
    const double elapsed=time-last_time;
    if(!std::isfinite(pos) || !std::isfinite(length) || !std::isfinite(time) || pos<0 || length<=0) {
        eligible=false;pending_start=false;phase=ListenPhase::Tracking;
        last_time=time;last_position=pos;playing=is_playing;return {};
    }
    if(key!=identity) {
        const bool pending=phase==ListenPhase::AwaitingIdentity && time>=wrap_time && time-wrap_time<=2;
        const bool observed_start=pending && is_playing && std::fabs(length-wrap_length)<=1.0 &&
            pos>=wrap_position-0.1 && pos<=wrap_position+(time-wrap_time)+1.5;
        const double next_start=observed_start ? wrap_start : time-pos;
        if(NaturalEnd(*this,pending ? wrap_time-last_time : elapsed) &&
           (phase!=ListenPhase::AwaitingIdentity || pending))
            done=CompleteListen(*this,pending ? wrap_start : last_time+duration-last_position);
        BeginListen(*this,key,observed_start ? 0 : pos,length,is_playing,time,next_start);
        last_position=pos;return done;
    }
    if(key.empty()) {eligible=false;playing=is_playing;last_time=time;return {};}
    if(phase==ListenPhase::AwaitingIdentity) {
        const double waiting=time-wrap_time;
        const double expected=wrap_position+(time-wrap_time);
        // A title update gets a short bounded window. If the same identity
        // continues on a valid new timeline, this is a new repeat instance.
        if(waiting<0 || waiting>2 || !is_playing ||
           pos<wrap_position-0.1 || pos>expected+1.5) {
            eligible=false;phase=ListenPhase::Tracking;pending_start=false;
            last_time=time;last_position=pos;playing=is_playing;return {};
        } else if(waiting>=1.0) {
            if(std::fabs(length-duration)<=1.0 && std::fabs(length-wrap_length)<=1.0) {
                done=CompleteListen(*this,wrap_start);
                const double start=wrap_start;
                BeginListen(*this,key,0,length,is_playing,time,start);
                last_position=pos;return done;
            }
            if(waiting<2) {transient=true;return {};}
            eligible=false;phase=ListenPhase::Tracking;pending_start=false;
            last_time=time;last_position=pos;playing=is_playing;return {};
        } else {transient=true;return {};}
    } else if(NaturalEnd(*this,elapsed) && is_playing && pos<=1.5) {
        phase=ListenPhase::AwaitingIdentity;wrap_time=time;wrap_start=time-pos;
        wrap_position=pos;wrap_length=length;transient=true;return {};
    }
    if(pos<=1.5 && pos<last_position-1.5) {
        // A seek/reset starts new coverage but can never finish the old listen.
        BeginListen(*this,key,pos,length,is_playing,time,time-pos);return {};
    }
    if(pending_start && time-identity_time<=2 && pos<=1.5) {
        start_time=time-pos;duration=length;eligible=true;pending_start=false;
        last_time=time;last_position=pos;playing=is_playing;return {};
    }
    const double expected=last_position+(playing ? elapsed : 0);
    if(elapsed<0 || elapsed>3 || pos<last_position-1.5 || pos>expected+1.5 ||
       std::fabs(length-duration)>1.0) eligible=false;
    if(eligible && playing && !is_playing && pos>=duration && expected>=duration-0.1)
        done=CompleteListen(*this,last_time+duration-last_position);
    last_time=time;last_position=pos;playing=is_playing;
    return done;
}
}
