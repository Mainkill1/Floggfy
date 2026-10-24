#include "../native/ogg_history_core.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <cmath>
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
static bool near(double actual,double expected) {return std::fabs(actual-expected)<1e-9;}
static Listen provisional_repeat(double wrap_position=0.1,double confirmation_position=1.2) {
    Listen listen;check(listen.Observe("previous",0,10,true,0).empty(),"issue4 initial observation");
    for(int i=1;i<10;++i)check(listen.Observe("previous",i,10,true,i).empty(),"issue4 continuous prior coverage");
    check(listen.Observe("previous",wrap_position,10,true,10+wrap_position).empty() && listen.transient,
          "issue4 restarted timeline waits for its title");
    check(listen.Observe("previous",confirmation_position,10,true,10+confirmation_position)=="previous",
          "issue4 provisional repeat emits prior completion once");
    check(listen.generation==2 && listen.completed_generation==1 && near(listen.start_time,10) &&
          near(listen.completed_start,0) && near(listen.completed_time,10) && listen.phase==ListenPhase::ProvisionalRepeat,
          "issue4 provisional label retains distinct current and completed generations");
    return listen;
}
static void issue4_cases() {
    // Exact reported ordering: reset10.1, apparent repeat11.2, real title11.8.
    auto delayed=provisional_repeat();
    check(delayed.Observe("actual",1.8,10,true,11.8).empty(),"late equal-duration title must not complete the old track twice");
    check(delayed.identity=="actual" && delayed.generation==2 && delayed.eligible && !delayed.transient &&
          delayed.phase==ListenPhase::Tracking && near(delayed.start_time,10) && delayed.completed_generation==1 &&
          near(delayed.completed_start,0) && near(delayed.completed_time,10),
          "late equal-duration title relabels existing coverage without creating another instance");
    for(int i=12;i<20;++i)check(delayed.Observe("actual",i-10,10,true,i).empty(),"reconciled coverage does not emit early completion");
    check(delayed.Observe("next",0,10,true,20)=="actual" && delayed.completed_generation==2 &&
          near(delayed.completed_start,10) && near(delayed.completed_time,20),
          "reconciled new title completes once with its observed start and own generation");
    check(delayed.Observe("next",0.2,10,true,20.2).empty() && delayed.completed_generation==2,
          "repeated metadata observation does not duplicate a reconciled completion");

    auto duration=provisional_repeat();
    check(duration.Observe("longer",1.8,20,true,11.8).empty() && duration.eligible && duration.generation==2 &&
          near(duration.start_time,10) && near(duration.duration,20) && duration.completed_generation==1,
          "duration arriving with delayed title preserves observed start and adopts actual duration");
    for(int i=12;i<30;++i)check(duration.Observe("longer",i-10,20,true,i).empty(),"new duration cannot finish at the old duration");
    check(duration.Observe("next",0,10,true,30)=="longer" && duration.completed_generation==2 && near(duration.completed_time,30),
          "different-duration reconciled track completes at its own end");

    Listen early;early.Observe("previous",0,10,true,0);
    for(int i=1;i<10;++i)early.Observe("previous",i,10,true,i);
    early.Observe("previous",.1,10,true,10.1);
    check(early.Observe("actual",.8,10,true,10.8)=="previous" && early.generation==2 && early.eligible && near(early.start_time,10),
          "title arriving before repeat confirmation still completes prior track exactly once");
    check(early.Observe("actual",1.2,10,true,11.2).empty() && early.completed_generation==1,
          "early title path cannot later emit a duplicate provisional completion");
    Listen different;different.Observe("previous",0,10,true,0);
    for(int i=1;i<10;++i)different.Observe("previous",i,10,true,i);
    different.Observe("previous",.2,20,true,10.2);
    check(different.Observe("actual",1.8,20,true,11.8)=="previous" && different.eligible &&
          different.generation==2 && near(different.start_time,10) && near(different.duration,20),
          "already observed different-duration transition retains its existing pending-title behavior");

    // The original window starts at the observed wrap, not confirmation or any
    // later observation. Binary-exact fractions exercise its inclusive edge.
    auto edge=provisional_repeat(.125,1.25);
    check(edge.Observe("actual",2.125,10,true,12.125).empty() && edge.generation==2 && edge.eligible,
          "relabel at original two-second window edge is allowed");
    auto expired=provisional_repeat(.125,1.25);
    check(expired.Observe("previous",2,10,true,12).empty(),"unchanged provisional label does not renew its deadline");
    check(expired.Observe("too-late",2.25,10,true,12.25).empty() && !expired.eligible && expired.generation==3 &&
          expired.completed_generation==1 && expired.phase==ListenPhase::Tracking,
          "late title outside original window cannot inherit provisional coverage");
    auto repeated=provisional_repeat();
    check(repeated.Observe("previous",2.2,10,true,12.2).empty() && repeated.eligible &&
          repeated.generation==2 && repeated.phase==ListenPhase::Tracking,
          "window expiration preserves a genuine same-title repeat's coverage");
    for(int i=13;i<20;++i)repeated.Observe("previous",i-10,10,true,i);
    repeated.Observe("previous",.1,10,true,20.1);
    check(repeated.Observe("previous",1.2,10,true,21.2)=="previous" && repeated.generation==3 &&
          repeated.completed_generation==2 && near(repeated.completed_start,10) && near(repeated.completed_time,20),
          "successive genuine repeats still complete exactly once per generation");
    Listen short_repeat;short_repeat.Observe("short",0,2,true,0);
    for(int i=1;i<8;++i)short_repeat.Observe("short",i*.25,2,true,i*.25);
    short_repeat.Observe("short",.125,2,true,2.125);
    check(short_repeat.Observe("short",1.25,2,true,3.25)=="short","short repeat first completion");
    short_repeat.Observe("short",1.75,2,true,3.75);short_repeat.Observe("short",.125,2,true,4.125);
    check(short_repeat.Observe("short",1.25,2,true,5.25)=="short" && short_repeat.completed_generation==2 &&
          near(short_repeat.completed_start,2) && near(short_repeat.completed_time,4),
          "a real next wrap inside the previous title window is not mistaken for a seek");

    auto backward=provisional_repeat();
    check(backward.Observe("previous",.1,10,true,11.3).empty() && !backward.eligible,
          "backward seek during provisional labeling discards inherited coverage");
    check(backward.Observe("actual",.6,10,true,11.8).empty() && backward.generation==3 &&
          near(backward.start_time,11.2) && backward.completed_generation==1,
          "title after a seek starts fresh coverage instead of relabeling the pre-seek instance");
    auto forward=provisional_repeat();
    check(forward.Observe("actual",5,10,true,11.8).empty() && !forward.eligible && forward.generation==3 &&
          forward.completed_generation==1,"forward seek cannot be reconciled as a continuous delayed title");
    auto changed_length=provisional_repeat();
    check(changed_length.Observe("previous",1.8,20,true,11.8).empty() && !changed_length.eligible &&
          changed_length.phase==ListenPhase::Tracking,"same-identity duration discontinuity remains conservative");
    auto gap=provisional_repeat();
    check(gap.Observe("actual",5.2,10,true,15.2).empty() && !gap.eligible && gap.generation==3 &&
          gap.completed_generation==1,"observation gap cannot seed delayed-title coverage or duplicate completion");
    auto invalidated=provisional_repeat();invalidated.eligible=false;
    check(invalidated.Observe("actual",1.8,10,true,11.8).empty() && !invalidated.eligible &&
          invalidated.generation==3 && invalidated.completed_generation==1,
          "external invalidation forbids relabeling already invalid provisional coverage");
    auto paused_label=provisional_repeat();paused_label.Observe("previous",1.4,10,false,11.4);
    check(paused_label.Observe("actual",1.4,10,false,11.8).empty() && paused_label.eligible &&
          !paused_label.playing && paused_label.generation==2 && near(paused_label.start_time,10) &&
          paused_label.completed_generation==1,"pause while title arrives preserves coverage without claiming another completion");
}
int main(int argc,char** argv) {
    if(argc==2 && !std::strcmp(argv[1],"--issue4")) {
        issue4_cases();std::puts("PASS: bounded delayed-title/repeat reconciliation and conservative seek/gap coverage");return 0;
    }
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
    Listen invalidated_wrap;invalidated_wrap.Observe("one",0,10,true,0);
    for(int i=1;i<10;++i)invalidated_wrap.Observe("one",i,10,true,i);
    invalidated_wrap.Observe("one",.1,10,true,10.1);invalidated_wrap.eligible=false;
    check(invalidated_wrap.Observe("one",1.2,10,true,11.2).empty() && invalidated_wrap.completed_generation==0 && !invalidated_wrap.transient,
          "failed observation or overflow invalidates a pending repeat completion");
    issue4_cases();
    std::puts("PASS: Ogg integrity and complete-listen cases");
}
