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
    Listen repeat;repeat.Observe("same",0,10,true,0);
    for(int i=1;i<10;++i)repeat.Observe("same",i,10,true,i);
    repeat.Observe("same",0.1,10,true,10.1);
    check(repeat.transient,"repeat waits briefly for delayed title");
    check(repeat.Observe("same",1.2,10,true,11.2)=="same","repeat-one completes first playback instance");
    check(repeat.generation==2 && repeat.completed_generation==1 && repeat.completed_start==0 && repeat.completed_time==10,
          "repeat completion retains old instance boundaries");
    for(int i=12;i<20;++i)repeat.Observe("same",i-10,10,true,i);
    repeat.Observe("same",0.1,10,true,20.1);
    check(repeat.Observe("same",1.2,10,true,21.2)=="same","repeat-one completes successive instances");
    check(repeat.generation==3 && repeat.completed_generation==2 && repeat.completed_start==10,
          "successive repeat generations stay distinct");
    Listen restart;restart.Observe("one",0,10,true,0);
    for(int i=1;i<=5;++i)restart.Observe("one",i,10,true,i);
    check(restart.Observe("one",0,10,true,5.2).empty() && restart.generation==2,
          "seek to start discards old coverage and begins a separate instance");
    for(int i=1;i<10;++i)restart.Observe("one",i,10,true,5.2+i);
    check(restart.Observe("two",0,10,true,15.2)=="one" && restart.completed_generation==2,
          "a full listen after seeking can complete only the new instance");
    Listen seek_tail;seek_tail.Observe("one",0,10,true,0);
    for(int i=1;i<10;++i)seek_tail.Observe("one",i,10,true,i);
    seek_tail.Observe("one",9.3,10,true,9.3);
    check(seek_tail.Observe("one",0,10,true,9.4).empty() && seek_tail.completed_generation==0,
          "seeking the tail to zero never credits a natural repeat");
    Listen gap_repeat;gap_repeat.Observe("one",0,10,true,0);
    for(int i=1;i<10;++i)gap_repeat.Observe("one",i,10,true,i);
    check(gap_repeat.Observe("one",0,10,true,20).empty() && gap_repeat.completed_generation==0,
          "an unobserved gap cannot complete a repeat");
    Listen paused;paused.Observe("one",0,10,true,0);
    for(int i=1;i<10;++i)paused.Observe("one",i,10,true,i);
    paused.Observe("one",9.5,10,false,9.5);paused.Observe("one",9.5,10,false,11);
    check(paused.Observe("one",0,10,true,11.2).empty() && paused.completed_generation==0,
          "pause then seek to start is not a completed repeat");
    Listen ambiguous;ambiguous.Observe("one",0,10,true,0);
    for(int i=1;i<10;++i)ambiguous.Observe("one",i,10,true,i);
    ambiguous.Observe("one",0.2,20,true,10.2);
    check(ambiguous.Observe("one",2.3,20,true,12.3).empty() && !ambiguous.eligible && !ambiguous.transient,
          "unresolved different-duration title ambiguity is bounded");
    Listen late_title;late_title.Observe("one",0,10,true,0);
    for(int i=1;i<10;++i)late_title.Observe("one",i,10,true,i);
    late_title.Observe("one",0.2,20,true,10.2);
    check(late_title.Observe("two",1.8,20,true,11.8)=="one" && late_title.eligible && late_title.start_time==10,
          "delayed title uses already observed next-instance start without losing coverage");
    Listen pause_end;pause_end.Observe("one",0,10,true,0);
    for(int i=1;i<=5;++i)pause_end.Observe("one",i,10,true,i);
    pause_end.Observe("one",5,10,false,5);pause_end.Observe("one",5,10,false,7);
    pause_end.Observe("one",5,10,true,7);
    for(int i=6;i<10;++i)pause_end.Observe("one",i,10,true,7+i-5);
    check(pause_end.Observe("two",0,10,true,12)=="one" && pause_end.completed_time==12,
          "pause duration cannot shift completed boundary to an earlier or new instance");
    std::puts("PASS: Ogg integrity and complete-listen cases");
}
