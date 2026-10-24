#include "recording_fixture.h"
#include "../native/recording_file.h"
#include "../native/flac_history_core.h"
#include "../native/vorbis_headers.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
using namespace history;
static void check(bool value,const char* label) { if(!value) { std::fprintf(stderr,"FAIL: %s\n",label); std::exit(1); } }
using recording_fixture::fixture;
static void vorbis_headers() {
    const auto id=recording_fixture::identification(),setup=recording_fixture::setup();
    check(ValidateVorbisHeaders(id,setup)==VorbisHeadersResult::Valid,"complete Vorbis I headers accepted without audio decoding");
    check(ValidateVorbisHeaders(id,recording_fixture::setup(2,1))==VorbisHeadersResult::Valid,"fully populated two-entry Huffman tree accepted");
    std::vector<uint8_t> owned_comment={3,'v','o','r','b','i','s',0,0,0,0,2,0,0,0};
    for(const std::string field:{"HISTORY_COMPLETE_LISTEN=1","HISTORY_CAPTURE=native compressed Ogg pages"}) {
        for(unsigned i=0;i<4;++i)owned_comment.push_back(uint8_t(field.size()>>(8*i)));
        owned_comment.insert(owned_comment.end(),field.begin(),field.end());
    }
    owned_comment.push_back(1);
    const auto owned_valid=recording_fixture::ogg_headers(id,setup,false,owned_comment);
    RecordingFile owned_recording;
    check(InspectRecording(owned_valid.data(),owned_valid.size(),owned_recording)&&owned_recording.owned&&owned_recording.complete,
          "supported headers preserve ownership for a complete captured recording");
    auto rejected=[&](const std::vector<uint8_t>& identification,const std::vector<uint8_t>& configuration,const char* label) {
        const auto stream=recording_fixture::ogg_headers(identification,configuration,false,owned_comment);RecordingFile recording;
        check(!InspectRecording(stream.data(),stream.size(),recording)&&!recording.owned&&!recording.complete,label);
    };
    rejected(id,{5,'v','o','r','b','i','s'},"signature-only setup with valid Ogg CRC/EOS cannot suppress a save");
    // Three fixed regressions: truncated codebook, mapping, and framing byte.
    for(const size_t length:{size_t(15),size_t(40),setup.size()-1})
        rejected(id,std::vector<uint8_t>(setup.begin(),setup.begin()+length),"truncated setup rejected despite valid container framing");
    auto bad=setup;bad[8]^=1;rejected(id,bad,"invalid codebook sync rejected");
    rejected(id,recording_fixture::setup(1,2),"single-entry codebook must use a one-bit codeword");
    rejected(id,recording_fixture::setup(2,2),"underpopulated Huffman tree rejected");
    rejected(id,recording_fixture::setup(3,1),"overpopulated Huffman tree rejected");
    bad=setup;bad.back()=0;rejected(id,bad,"missing setup framing bit rejected");
    bad=setup;bad.push_back(0);rejected(id,bad,"trailing setup bytes are not silently treated as validated configuration");
    // Bit positions in this deliberately minimal fixture, after the 7-byte signature.
    auto bit=[&](std::vector<uint8_t>& packet,unsigned position) {position+=56;packet[position/8]|=uint8_t(1u<<(position%8));};
    bad=setup;bit(bad,339);rejected(id,bad,"mode referencing a nonexistent mapping rejected");
    bad=setup;bit(bad,307);rejected(id,bad,"reserved nonzero mode window type rejected");
    bad=setup;bad[11]=bad[12]=0;rejected(id,bad,"zero-dimensional populated codebook rejected");
    bad=setup;bad[13]=bad[14]=bad[15]=255;
    check(ValidateVorbisHeaders(id,bad)==VorbisHeadersResult::Unsupported,"oversized codebook rejected before allocation or expansion");
    rejected(id,bad,"unsupported owned codebook cannot authorize repair or lower-quality replacement");
    auto invalid_id=id;invalid_id[28]=0x55;rejected(invalid_id,setup,"invalid Vorbis block sizes rejected");
    invalid_id=id;invalid_id[29]=0;rejected(invalid_id,setup,"missing identification framing bit rejected");
    invalid_id=id;invalid_id[11]=9;
    check(ValidateVorbisHeaders(invalid_id,setup)==VorbisHeadersResult::Unsupported,"channel resource limit is not reported as validated audio");
    rejected(invalid_id,setup,"unsupported owned channel count does not confer ownership");
    bad.assign(1024*1024+1,0);
    check(ValidateVorbisHeaders(id,bad)==VorbisHeadersResult::Unsupported,"setup input limit checked before parser allocation");
}
int main(int argc,char** argv) {
    vorbis_headers();
    RecordingFile file;
    auto ogg=recording_fixture::ogg();
    check(InspectRecording(ogg.data(),ogg.size(),file)&&file.complete,"complete Ogg packet and EOS coverage validated");
    ogg=recording_fixture::ogg(true);
    check(InspectRecording(ogg.data(),ogg.size(),file)&&!file.complete,"repeated BOS cannot reset coverage to hide incomplete stream");
    ogg=recording_fixture::ogg();ogg.pop_back();
    check(!InspectRecording(ogg.data(),ogg.size(),file)||!file.complete,"truncated Ogg audio cannot suppress save");
    ogg=recording_fixture::ogg();ogg.back()^=1;
    check(!InspectRecording(ogg.data(),ogg.size(),file)||!file.complete,"corrupt audio-page CRC cannot suppress save");
    if(argc>1 && std::string(argv[1])=="--issue6") {
        std::puts("PASS: supported Vorbis header validation, conservative ownership, Ogg CRC/EOS coverage");
        return 0;
    }
    auto bytes=fixture();
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
