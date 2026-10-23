#include "recording_fixture.h"
#include "../native/recording_file.h"
#include "../native/flac_history_core.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
using namespace history;
static void check(bool value,const char* label) { if(!value) { std::fprintf(stderr,"FAIL: %s\n",label); std::exit(1); } }
using recording_fixture::fixture;
int main(int argc,char** argv) {
    auto bytes=fixture(); RecordingFile file;
    auto ogg=recording_fixture::ogg();
    check(InspectRecording(ogg.data(),ogg.size(),file)&&file.complete,"complete Ogg packet and EOS coverage validated");
    ogg=recording_fixture::ogg(true);
    check(InspectRecording(ogg.data(),ogg.size(),file)&&!file.complete,"repeated BOS cannot reset coverage to hide incomplete stream");
    ogg=recording_fixture::ogg();ogg.pop_back();
    check(!InspectRecording(ogg.data(),ogg.size(),file)||!file.complete,"truncated Ogg audio cannot suppress save");
    check(InspectRecording(bytes.data(),bytes.size(),file)&&file.complete,"complete FLAC frames and checksums validated");
    auto header=bytes; header.resize(42);
    check(InspectRecording(header.data(),header.size(),file)&&!file.complete,"header-only FLAC is not a recording");
    auto broken=bytes; broken.back()^=1;
    check(InspectRecording(broken.data(),broken.size(),file)&&!file.complete,"corrupt frame CRC cannot suppress save");
    broken=bytes; broken.pop_back();
    check(InspectRecording(broken.data(),broken.size(),file)&&!file.complete,"interrupted final frame detected");
    Catalog catalog; catalog.title=L"Track";catalog.artist=L"Artist";catalog.album=L"Album";catalog.track=1;catalog.duration=1;
    Tags tags; tags.fields={{"TITLE","Track"},{"ARTIST","Artist"},{"ALBUM","Album"},{"TRACKNUMBER","1"},{"HISTORY_MEDIA_DURATION","1"},{"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_CAPTURE","native compressed FLAC input"}};
    std::vector<uint8_t> tagged; std::string error;
    check(TagFlac(bytes,tags,tagged,error),"owned fixture tagged without artwork");
    check(InspectRecording(tagged.data(),tagged.size(),file)&&file.complete&&file.owned&&MatchesRecording(file,catalog),"full legacy identity and complete recording recognized");
    auto other=catalog; other.disc=2;
    check(!MatchesRecording(file,other),"different discs never upgrade one another");
    other=catalog; other.duration=100;
    check(!MatchesRecording(file,other),"different durations never upgrade one another");
    other=catalog;other.title+=L" different";
    check(!MatchesRecording(file,other),"full title distinguishes truncated path collisions");
    tags.fields.push_back({"SPOTIFY_URI","spotify:track:one"});
    check(TagFlac(bytes,tags,tagged,error)&&InspectRecording(tagged.data(),tagged.size(),file),"URI fixture");
    catalog.spotify_uri="spotify:track:two";
    check(!MatchesRecording(file,catalog),"different URI wins over matching textual metadata");
    catalog.spotify_uri="spotify:track:one";
    check(MatchesRecording(file,catalog),"matching stable URI recognized");
    other=catalog; other.spotify_uri="spotify:track:two";
    check(RecordingSuffix(catalog)!=RecordingSuffix(other),"stable suffix distinguishes recording identity");
    if(argc>1) {
        std::ifstream input(argv[1],std::ios::binary); bytes.assign(std::istreambuf_iterator<char>(input),{});
        check(InspectRecording(bytes.data(),bytes.size(),file)&&file.complete,"real complete captured file validated");
        std::printf("Validated %zu bytes, %u Hz, %u bits\n",bytes.size(),file.quality.rate,file.quality.bits);
    }
    std::puts("PASS: complete recording integrity and conservative identity matching");
}
