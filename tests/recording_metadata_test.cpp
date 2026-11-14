#include "../native/recording_metadata.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace history;
static Media Track(const char* uri,const char* instance="") {
    Media media;media.source=MediaSource::Spotify;media.spotify_uri=uri;
    media.playback_instance=instance;media.title=L"Song";media.artist=L"Artist";
    media.album=L"Album";media.duration=200;media.playing=true;return media;
}
static void FallbackAlternationCountsEveryStart() {
    RecordingHistory history;
    const auto first=history.Observe(Track("spotify:track:A"),0).metadata.generation;
    history.MarkAssigned(first);
    std::uint64_t previous=first;
    for(unsigned start=2;start<=23;++start) {
        const auto& record=history.Observe(Track(start%2?"spotify:track:A":"spotify:track:B"),start);
        assert(record.metadata.generation>previous&&"a return must create a new occurrence");
        assert(!record.assigned&&"assignment belongs to one occurrence");
        previous=record.metadata.generation;
    }
    assert(history.Starts()==23);
}
static void PauseAndSnapshotsKeepOccurrence() {
    RecordingHistory history;auto media=Track("spotify:track:A");
    const auto generation=history.Observe(media,10).metadata.generation;
    media.position=140;history.Observe(media,11); // Duplicate/live position refresh.
    media.playing=false;history.Observe(media,12);
    history.Prune();assert(history.Find(generation));
    media.playing=true;media.position=20; // A backseek alone does not prove a restart.
    assert(history.Observe(media,100000).metadata.generation==generation);
    assert(history.Starts()==1&&history.Records().size()==1);
}
static void StaleObservationCannotReturnToOldOccurrence() {
    RecordingHistory history;
    const auto old=history.Observe(Track("spotify:track:A","native-A"),10).metadata.generation;
    const auto current=history.Observe(Track("spotify:track:B","native-B"),20).metadata.generation;
    history.Observe(Track("spotify:track:A","native-A"),15);
    assert(history.Current()->metadata.generation==current&&"stale snapshots cannot replace current occurrence");
    assert(history.Starts()==2&&history.Find(old));
    history.Observe(Track("spotify:track:C","native-C"),16);
    assert(history.Current()->metadata.generation==current&&history.Starts()==2);
    assert(history.Records().size()==2&&"stale unseen items must not create records");
}
static void LateNativeIdentityAttachesToContinuousOccurrence() {
    RecordingHistory history;auto media=Track("spotify:track:A");
    const auto generation=history.Observe(media,10).metadata.generation;
    history.MarkAssigned(generation);
    media.playback_instance="native-A";media.title.clear();media.artist.clear();media.album.clear();
    const auto& attached=history.Observe(media,11);
    assert(attached.metadata.generation==generation&&attached.assigned);
    assert(attached.metadata.media.playback_instance=="native-A"&&attached.metadata.media.title==L"Song");
    assert(history.Starts()==1&&history.Records().size()==1);
    media.playback_instance.clear();history.Observe(media,12);
    assert(history.Current()->metadata.generation==generation);
    assert(history.Current()->metadata.media.playback_instance=="native-A");
    media.playback_instance="native-B";
    assert(history.Observe(media,13).metadata.generation!=generation&&history.Starts()==2);
}
static void NativeInstancesRemainAuthoritative() {
    RecordingHistory history;auto media=Track("spotify:track:A","native-A");
    const auto generation=history.Observe(media,10).metadata.generation;
    media.title=L"Richer title";media.album_artist=L"Album artist";media.track=7;
    assert(history.Observe(media,11).metadata.generation==generation);
    media.title.clear();media.album_artist.clear();media.track=0;
    const auto& sparse=history.Observe(media,12);
    assert(sparse.metadata.media.title==L"Richer title"&&sparse.metadata.media.track==7);
    media.playback_instance="native-B";
    assert(history.Observe(media,13).metadata.generation!=generation&&history.Starts()==2);
}
static void RetirementCountsSubsequentStarts() {
    RecordingHistory history;const auto first=history.Observe(Track("spotify:track:A"),0).metadata.generation;
    for(unsigned start=2;start<=5;++start) {
        history.Observe(Track(start%2?"spotify:track:A":"spotify:track:B"),start);history.Prune(5);
        assert(history.Find(first));
    }
    history.Observe(Track("spotify:track:B"),6);history.Prune(5);
    assert(!history.Find(first)&&history.Starts()==6);
}
int main(int argc,char** argv) {
    if(argc==1||!strcmp(argv[1],"alternation"))FallbackAlternationCountsEveryStart();
    if(argc==1||!strcmp(argv[1],"pause"))PauseAndSnapshotsKeepOccurrence();
    if(argc==1||!strcmp(argv[1],"stale"))StaleObservationCannotReturnToOldOccurrence();
    if(argc==1||!strcmp(argv[1],"late-id"))LateNativeIdentityAttachesToContinuousOccurrence();
    if(argc==1||!strcmp(argv[1],"native"))NativeInstancesRemainAuthoritative();
    if(argc==1||!strcmp(argv[1],"retirement"))RetirementCountsSubsequentStarts();
    puts("PASS: recording occurrence identity, start counting and retirement");
}
