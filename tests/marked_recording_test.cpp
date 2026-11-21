#include "../native/marked_recording.h"
#include "../native/metadata_update_core.h"
#include "../native/aac_stream.h"
#include "../native/flac_history_core.h"
#include "../native/mp3_stream.h"
#include "recording_fixture.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdio>

using namespace history;
namespace {
using Bytes=std::vector<uint8_t>;
constexpr char uri[]="spotify:track:0123456789abcdefghijkl";
constexpr char stamp[]="2024-02-29T23:59:59Z";
constexpr const char* captures[]={"native compressed FLAC input","native compressed Ogg pages",
                                  "native compressed MP3 input","native compressed SpAC AAC packets"};
Tags TagsFor(unsigned format) {
    Tags tags;
    tags.fields={{"SPOTIFY_URI",uri},{"HISTORY_COMPLETE_STREAM","1"},
                 {"HISTORY_CAPTURE",captures[format]},{"HISTORY_SAMPLES","3456"}};
    return tags;
}
Bytes Encode(unsigned format,const Tags& tags) {
    Bytes source;
    std::string error;
    if(format==0)assert(TagFlac(recording_fixture::fixture(),tags,source,error));
    if(format==1)assert(TagOgg(recording_fixture::ogg(),tags,source,error));
    if(format==2) {
        Bytes audio(417*3);
        for(size_t at=0;at<audio.size();at+=417) {
            audio[at]=0xff;audio[at+1]=0xfb;audio[at+2]=0x90;
        }
        assert(MakeOwnedMp3(audio.data(),audio.size(),tags,source,error));
    }
    if(format==3) {
        AacInfo info;
        info.rate=44100;info.channels=2;info.samples_per_packet=2048;
        info.priming=5119;info.total_samples=8192;info.config={0xeb,0x8a,0x08,0x00};
        const std::vector<Bytes> packets((info.total_samples+info.priming+2047)/2048,Bytes(4096,0xa5));
        assert(MakeAacM4a(info,packets,tags,source,error));
    }
    return source;
}
bool Read(const Bytes& bytes,MarkedRecording& result,size_t* total=nullptr) {
    std::string error;
    const bool ok=ReadMarkedRecording(bytes.size(),[&](uint64_t at,void* out,size_t n){
        assert(at<=bytes.size() && n<=bytes.size()-at);
        if(total)*total+=n;
        std::memcpy(out,bytes.data()+at,n);
        return true;
    },result,error);
    assert(ok==error.empty());
    return ok;
}
void RejectMissingAudio(const Bytes& bytes,unsigned format) {
    MarkedRecording mark;
    auto truncated=bytes;
    if(format==0) {
        size_t at=4;
        bool last=false;
        do {
            last=(bytes[at]&128)!=0;
            at+=4+(size_t(bytes[at+1])<<16)+(size_t(bytes[at+2])<<8)+bytes[at+3];
        } while(!last);
        truncated.resize(at+2); // The sync bytes survive, but the frame does not.
        assert(!Read(truncated,mark));
    } else if(format==1) {
        size_t at=0;
        unsigned packets=0;
        while(at<bytes.size()) {
            const unsigned count=bytes[at+26];
            size_t body=at+27+count;
            for(unsigned lace=0;lace<count;++lace) {
                const auto length=bytes[at+27+lace];
                body+=length;
                if(length==255)continue;
                ++packets;
                if(packets==2) {
                    truncated.resize(body);
                    // This lace claims more page payload after the complete comment.
                    assert(count<255);
                    truncated[at+26]=uint8_t(count+1);
                    truncated.insert(truncated.begin()+at+27+count,1);
                    assert(!Read(truncated,mark));
                }
                if(packets==3) {
                    truncated=bytes;
                    truncated.resize(body); // Header packets survive without audio.
                    assert(!Read(truncated,mark));
                    return;
                }
            }
            at=body;
        }
        assert(false);
    } else if(format==2) {
        size_t at=10+(size_t(bytes[6])<<21)+(size_t(bytes[7])<<14)+(size_t(bytes[8])<<7)+bytes[9];
        truncated.resize(at);
        truncated.push_back(1); // Intact ID3 with one arbitrary byte after it.
        assert(!Read(truncated,mark));
        truncated=bytes;
        truncated.resize(at+4); // A real MPEG header cannot fit its frame.
        assert(!Read(truncated,mark));
    } else {
        for(size_t at=0;at<bytes.size();) {
            const auto size=(size_t(bytes[at])<<24)|(size_t(bytes[at+1])<<16)|(size_t(bytes[at+2])<<8)|bytes[at+3];
            assert(size>=8 && size<=bytes.size()-at);
            if(!std::memcmp(bytes.data()+at+4,"mdat",4)) {
                truncated.erase(truncated.begin()+at,truncated.begin()+at+size);
                assert(!Read(truncated,mark));
                return;
            }
            at+=size;
        }
        assert(false);
    }
}
}
int main() {
    assert(ValidMarkedUri(uri) && !ValidMarkedUri("spotify:track:abc"));
    assert(!ValidMarkedUri("spotify:episode:0123456789abcdefghijkl"));
    assert(ValidMarkedDate(stamp) && !ValidMarkedDate("1900-02-29T00:00:00Z"));
    assert(!ValidMarkedDate("2024-02-29T24:00:00Z") && !ValidMarkedDate("2024-00-01T00:00:00Z"));

    Catalog catalog;catalog.spotify_uri=uri;
    for(unsigned format=0;format<4;++format) {
        auto tags=TagsFor(format);
        auto bytes=Encode(format,tags);
        MarkedRecording mark;
        assert(Read(bytes,mark) && mark.uri==uri && mark.owned && mark.complete && mark.saved_utc.empty());
        RecordingFile inspected;
        assert(InspectRecording(bytes.data(),bytes.size(),inspected));
        assert(inspected.complete && inspected.owned);
        assert(!NeedsRecordingMetadata(inspected,{}));
        assert(NeedsRecordingMetadata(inspected,{},true));

        std::string error;
        Bytes updated;
        const Tags incoming{{{"HISTORY_SAVED_UTC",stamp}},{},""};
        assert(FillRecordingMetadata(bytes,incoming,catalog,updated,error)==MetadataUpdate::Updated);
        assert(Read(updated,mark) && mark.saved_utc==stamp);
        RejectMissingAudio(updated,format);
        const Tags replacement{{{"HISTORY_SAVED_UTC","2025-01-01T00:00:00Z"}},{},""};
        Bytes ignored;
        assert(FillRecordingMetadata(updated,replacement,catalog,ignored,error)==MetadataUpdate::Unchanged);
        if(format==2) {
            const auto added=updated.size()-bytes.size();
            assert(std::equal(bytes.begin()+10,bytes.end(),updated.begin()+10+added));
        }

        tags.fields.emplace_back("HISTORY_SAVED_UTC","2024-02-30T23:59:59Z");
        bytes=Encode(format,tags);
        assert(!Read(bytes,mark));
        assert(FillRecordingMetadata(bytes,incoming,catalog,ignored,error)==MetadataUpdate::Unchanged);

        tags.fields.back().second=stamp;
        if(format==1)tags.fields.emplace_back("METADATA_BLOCK_PICTURE",std::string(200000,'A'));
        if(format==2)tags.cover=Bytes(200000,'A');
        bytes=Encode(format,tags);
        size_t read=0;
        assert(Read(bytes,mark,&read) && mark.saved_utc==stamp && mark.bytes==bytes.size());
        assert(read<4096);
        if(format==0) {
            bytes[5]=bytes[6]=bytes[7]=0xff;
            assert(!Read(bytes,mark));
        } else if(format==1) {
            bytes[26]=255;
            assert(!Read(bytes,mark));
        } else if(format==2) {
            bytes[6]=0xff;
            assert(!Read(bytes,mark));
        } else {
            bytes[0]=bytes[1]=bytes[2]=bytes[3]=0xff;
            assert(!Read(bytes,mark));
        }
    }
    std::puts("marked recording tests passed");
}
