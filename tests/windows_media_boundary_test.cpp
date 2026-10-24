// Basic media observation boundary only: no live SMTC, artwork or disk access.
#include "../native/media_session.h"
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
}
int RunMediaSnapshotTests() {
    using namespace history;
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
    std::puts("PASS: blocked media I/O cannot stall capture-style polling; snapshots are fresh, sequenced once, and stopped state survives outstanding work");
    return 0;
}
#ifndef FLOGGFY_COMBINED_TEST
int main(){return RunMediaSnapshotTests();}
#endif
