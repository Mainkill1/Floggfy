// Basic media observation boundary only: no live SMTC, artwork or disk access.
#include "../native/media_session.h"
#include "../native/media_timeline.h"
#include "../native/ogg_history_core.h"
#include <atomic>
#include <cassert>
#include <cstdio>
namespace {
struct Source {
    std::atomic<unsigned> calls{0},track{1};
    std::atomic<bool> block{false},fail{false};
    HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Source(){assert(entered&&release);}
    ~Source(){CloseHandle(entered);CloseHandle(release);}
};
bool Fetch(history::Media& media,double& observed,void* context) {
    auto& source=*static_cast<Source*>(context);++source.calls;
    if(source.block.load()) {
        SetEvent(source.entered);
        assert(WaitForSingleObject(source.release,10000)==WAIT_OBJECT_0);
    }
    if(source.fail.load())return false;
    media.title=L"Track "+std::to_wstring(source.track.load());
    media.artist=L"Artist";media.duration=180;media.position=1;media.playing=true;
    observed=double(GetTickCount64())/1000;return true;
}
template<class Predicate> void Wait(Predicate ready) {
    const auto deadline=GetTickCount64()+6000;
    while(!ready()){assert(GetTickCount64()<deadline);Sleep(1);}
}
void TimelineContinuity() {
    using history::media_detail::MediaTimeline;
    constexpr int64_t second=10000000,utc=130000000000000000LL;
    MediaTimeline before{0,180*second,10*second,utc,4};
    assert(before.ContinuesTo(before));
    auto after=before;after.position+=second/2;after.updated+=second/2;
    assert(before.ContinuesTo(after)); // A normal refresh during the async call.
    history::Listen listen;
    auto current=before;current.position=0;
    assert(listen.Observe("A",0,180,true,100).empty()&&listen.eligible);
    for(unsigned i=1;i<=12;++i) {
        auto next=current;next.position+=second/2;next.updated+=second/2;
        assert(current.ContinuesTo(next));
        assert(listen.Observe("A",double(next.position)/second,180,true,100+i*0.5).empty()&&listen.eligible);
        current=next;
    }
    // Every pair changes Position/LastUpdatedTime for six seconds; no starvation.
    assert(listen.last_time==106&&listen.last_position==6);
    listen.Observe("A",10,180,true,110);assert(!listen.eligible); // Real observation gaps still fail.
    after=before;after.updated+=second/2;after.position+=second/2+second/5;
    assert(before.ContinuesTo(after)); // Bounded rounding jitter.
    after.position+=second;assert(!before.ContinuesTo(after)); // Forward seek.
    after=before;after.position-=second;after.updated+=second/2;
    assert(!before.ContinuesTo(after)); // Backward seek.
    after=before;after.updated-=1;assert(!before.ContinuesTo(after));
    after=before;after.end+=second;assert(!before.ContinuesTo(after));
    after=before;after.start+=1;assert(!before.ContinuesTo(after));
    after=before;after.status=3;assert(!before.ContinuesTo(after));
    auto paused=before;paused.status=3;after=paused;after.updated+=second;
    assert(paused.ContinuesTo(after)); // Paused timeline timestamp refresh.
    after.position+=1;assert(!paused.ContinuesTo(after));
    after=before;after.position=after.end+1;assert(!before.ContinuesTo(after));
    after=before;after.updated=0;assert(!before.ContinuesTo(after));
    auto ending=before;ending.position=ending.end-second/4;
    after=ending;after.updated+=second;after.position=after.end;
    assert(ending.ContinuesTo(after)); // Natural clamping at the end.
    after.position=0;assert(!ending.ContinuesTo(after)); // Equal-duration next track or repeat.
}
}
int RunMediaSnapshotTests() {
    using namespace history;
    TimelineContinuity();
    Source source;MediaSnapshotReader reader(Fetch,&source);
    Media out;out.title=L"untouched";double observed=0;uint64_t sequence=0;
    assert(reader.TryRead(out,observed,sequence)==MediaRead::Pending&&out.title==L"untouched"&&sequence==0);
    assert(reader.Start()&&reader.Start());
    Wait([&]{return reader.TryRead(out,observed,sequence)==MediaRead::Ready;});
    assert(out.title==L"Track 1"&&observed>0&&sequence>0);
    const auto first_time=observed;const auto first_sequence=sequence;
    assert(reader.TryRead(out,observed,sequence)==MediaRead::Pending&&observed==first_time&&sequence==first_sequence);
    source.block=true;source.track=2;
    Wait([&]{return WaitForSingleObject(source.entered,0)==WAIT_OBJECT_0;});
    const auto blocked_calls=source.calls.load();
    const auto start=GetTickCount64();unsigned produced=0,drained=0;
    // The media callback remains blocked for more than the listen-gap bound.
    // Capture-style progress and RAM-only polling must continue during it.
    while(GetTickCount64()-start<3100) {
        ++produced;
        assert(reader.TryRead(out,observed,sequence)==MediaRead::Pending);
        ++drained;Sleep(1);
    }
    assert(produced==drained&&drained>100&&source.calls==blocked_calls&&sequence==first_sequence);
    uint64_t unconsumed=0;Media stale;stale.title=L"retain";double stale_time=0;
    assert(reader.TryRead(stale,stale_time,unconsumed)==MediaRead::Failed&&unconsumed==first_sequence&&stale.title==L"retain");
    SetEvent(source.release);
    Wait([&]{return reader.TryRead(out,observed,sequence)==MediaRead::Ready;});
    assert(out.title==L"Track 2"&&sequence>first_sequence&&observed-first_time>=3);
    const auto second=sequence;
    source.fail=true;
    Wait([&]{return reader.TryRead(out,observed,sequence)==MediaRead::Failed;});
    assert(sequence>second&&out.title==L"Track 2");
    reader.Stop();Wait([&]{return reader.IsStopped();});
    assert(reader.TryRead(out,observed,sequence)==MediaRead::Pending);
    // A stopped observer cannot start a second live thread behind a blocked
    // first one, and Stop/destruction never waits for the external operation.
    source.fail=false;source.block=true;ResetEvent(source.entered);ResetEvent(source.release);
    assert(reader.Start());Wait([&]{return WaitForSingleObject(source.entered,0)==WAIT_OBJECT_0;});
    const auto stop_start=GetTickCount64();reader.Stop();
    assert(GetTickCount64()-stop_start<1000&&!reader.Start()&&!reader.IsStopped());
    SetEvent(source.release);Wait([&]{return reader.IsStopped();});
    std::puts("PASS: normal timeline refreshes retain coverage; seeks/transitions retry; blocked media I/O cannot stall polling; snapshots are fresh, sequenced once, and stop safely");
    return 0;
}
#ifndef FLOGGFY_COMBINED_TEST
int main(){return RunMediaSnapshotTests();}
#endif
