#include "../native/auto_skip.h"
#include <cassert>
#include <cstdio>
using namespace history;

int main() {
    const Quality saved_ogg{Codec::Vorbis,44100,2,0,320000};
    const Quality saved_flac{Codec::Flac,44100,2,16,0};
    PlaybackQualitySnapshot current;
    current.identity=7;current.format=PlaybackFormat::Ogg;current.rate=44100;current.channels=2;
    assert(!ShouldAutoSkipDownloaded(current,8,saved_ogg));
    assert(ShouldAutoSkipDownloaded(current,7,saved_ogg));
    current.format=PlaybackFormat::Unknown;
    assert(!ShouldAutoSkipDownloaded(current,7,saved_ogg));
    current.format=PlaybackFormat::Ogg;current.channels=0;
    assert(!ShouldAutoSkipDownloaded(current,7,saved_ogg));
    current.format=PlaybackFormat::Flac;current.rate=48000;current.channels=2;current.bits=24;
    assert(!ShouldAutoSkipDownloaded(current,7,saved_ogg));
    assert(!ShouldAutoSkipDownloaded(current,7,saved_flac));
    current.rate=44100;current.bits=16;
    assert(ShouldAutoSkipDownloaded(current,7,saved_flac));
    current.rate=44100;current.bits=24;
    assert(!ShouldAutoSkipDownloaded(current,7,saved_flac));
    current.rate=44100;current.bits=16;
    assert(!ShouldAutoSkipDownloaded(current,7,saved_ogg));

    assert(!QueueAutoSkipRequest("not-a-track",3));
    assert(ReadAutoSkipRequest().token==0);
    const auto first=QueueAutoSkipRequest("spotify:track:0000000000000000000001",3);
    assert(first);
    auto request=ReadAutoSkipRequest();
    assert(request.token==first&&request.settings_generation==3&&
           request.uri=="spotify:track:0000000000000000000001");
    const auto second=QueueAutoSkipRequest("spotify:track:0000000000000000000002",4);
    request=ReadAutoSkipRequest();
    assert(second>first&&request.token==second&&request.settings_generation==4&&
           request.uri=="spotify:track:0000000000000000000002");
    std::puts("PASS: auto-skip quality and bounded request evidence fail closed");
}
