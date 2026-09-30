#include "../native/ogg_history_core.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
using namespace history;
static void check(bool value, const char* name) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}
static void put(std::vector<uint8_t>& v, unsigned at, uint64_t n, unsigned count) {
    for(unsigned i=0;i<count;i++) v[at+i] = uint8_t(n >> (i*8));
}
// Fixture CRC is deliberately independent of the production implementation.
static void checksum(std::vector<uint8_t>& v) {
    put(v,22,0,4); uint32_t crc=0;
    for (auto b : v) { crc ^= uint32_t(b)<<24;
        for(int i=0;i<8;i++) crc = crc&0x80000000 ? (crc<<1)^0x04c11db7 : crc<<1;
    } put(v,22,crc,4);
}
static std::vector<uint8_t> page(uint32_t seq, uint8_t flags, int64_t samples,
                                 bool vorbis = false) {
    std::vector<uint8_t> v(58,0); std::memcpy(v.data(),"OggS",4);
    v[5]=flags; put(v,6,uint64_t(samples),8); put(v,14,1234,4); put(v,18,seq,4);
    v[26]=1; v[27]=30;
    if(vorbis) { std::memcpy(v.data()+28,"\x01vorbis",7); v[39]=2; put(v,40,44100,4); }
    checksum(v); return v;
}
static Result push(Stream& s, const std::vector<uint8_t>& v) { return s.Push(v.data(),v.size()); }
int main() {
    auto first=page(1,2,0,true), middle=page(2,0,44100), end=page(3,4,88200);
    Page p; check(ParsePage(first.data(),first.size(),p),"valid Ogg CRC accepted");
    check(p.vorbis_start && p.rate==44100 && p.channels==2,"Vorbis identification parsed");
    auto bad=first; bad.back()^=1;
    check(!ParsePage(bad.data(),bad.size(),p),"bad CRC rejected");
    check(!ParsePage(first.data(),first.size()-1,p),"truncated page rejected");
    Stream s; check(push(s,page(0,2,0))==Result::Ignore,"custom Spotify page ignored");
    check(push(s,first)==Result::Begin,"first Vorbis sequence may be one");
    check(push(s,middle)==Result::Append,"contiguous page appended");
    check(push(s,end)==Result::Complete && s.pages==3 && s.Duration()==2,"EOS publishes full stream");
    Stream gap; push(gap,first);
    check(push(gap,end)==Result::Invalid && !gap.complete,"page gap cannot publish");
    Stream partial; check(push(partial,middle)==Result::Ignore,"partial listen without BOS ignored");
    Stream corrupt; push(corrupt,first);
    check(push(corrupt,bad)==Result::Invalid,"CRC failure invalidates stream");
    Stream backwards; push(backwards,first); push(backwards,middle);
    check(push(backwards,page(3,4,100))==Result::Invalid,"backward granule rejected");
    Listen listen; listen.Observe("one",0,10,true,0);
    for(int i=1;i<10;i++) check(listen.Observe("one",i,10,true,i).empty(),"read-ahead not a completed listen");
    check(listen.Observe("two",0,10,true,10)=="one","natural track completion retained");
    Listen skip; skip.Observe("one",0,10,true,0); skip.Observe("one",4,10,true,4);
    check(skip.Observe("two",0,10,true,5).empty(),"skipped track rejected");
    Listen seek; seek.Observe("one",0,10,true,0); seek.Observe("one",8,10,true,1);
    seek.Observe("one",9,10,true,2);
    check(seek.Observe("two",0,10,true,3).empty(),"seek cannot manufacture full listen");
    Listen halfway; halfway.Observe("one",5,10,true,0); halfway.Observe("one",9,10,true,4);
    check(halfway.Observe("two",0,10,true,5).empty(),"starting halfway rejected");
    Listen delayed_metadata; delayed_metadata.Observe("one",0,10,true,0);
    for(int i=1;i<10;i++) delayed_metadata.Observe("one",i,10,true,i);
    delayed_metadata.Observe("one",9.7,10,true,9.7);
    delayed_metadata.Observe("one",0.2,20,true,10.2);
    check(delayed_metadata.Observe("two",0.6,20,true,10.6)=="one",
          "timeline may switch before media title at natural end");
    Listen tail_skip; tail_skip.Observe("one",0,10,true,0);
    for(int i=1;i<10;i++) tail_skip.Observe("one",i,10,true,i);
    tail_skip.Observe("one",9.3,10,true,9.3);
    check(tail_skip.Observe("two",0,10,true,9.4).empty(),"skipping the final fraction is incomplete");
    Listen ended; ended.Observe("one",0,10,true,0);
    for(int i=1;i<10;i++) ended.Observe("one",i,10,true,i);
    check(ended.Observe("one",10,10,false,10)=="one","last track completes without next song");
    std::puts("PASS: Ogg integrity and complete-listen cases");
}
