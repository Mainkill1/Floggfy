#include "../native/mp3_stream.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>
using namespace history;
static thread_local long allocation_allowance=-1;
void* operator new(std::size_t size) {
    if(allocation_allowance==0)throw std::bad_alloc();
    if(allocation_allowance>0)--allocation_allowance;
    if(void* value=std::malloc(size?size:1))return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size){return ::operator new(size);}
__attribute__((noinline)) void operator delete(void* value) noexcept {std::free(value);}
__attribute__((noinline)) void operator delete[](void* value) noexcept {::operator delete(value);}
__attribute__((noinline)) void operator delete(void* value,std::size_t) noexcept {::operator delete(value);}
__attribute__((noinline)) void operator delete[](void* value,std::size_t) noexcept {::operator delete(value);}
static void check(bool value,const char* message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static void syncsafe(uint8_t* out,size_t value) {
    out[0]=uint8_t(value>>21)&0x7f;out[1]=uint8_t(value>>14)&0x7f;
    out[2]=uint8_t(value>>7)&0x7f;out[3]=uint8_t(value)&0x7f;
}
static std::vector<uint8_t> id3(unsigned version,size_t payload) {
    std::vector<uint8_t> out(10);memcpy(out.data(),"ID3",3);out[3]=uint8_t(version);
    syncsafe(out.data()+6,payload);
    out.resize(10+payload,uint8_t(version));return out;
}
static std::vector<uint8_t> frame(uint8_t version_layer,uint8_t rate,unsigned length,uint8_t mode=0) {
    std::vector<uint8_t> out(length,0);out[0]=0xff;out[1]=version_layer;
    out[2]=rate;out[3]=mode;return out;
}
static void append(std::vector<uint8_t>& out,const std::vector<uint8_t>& bytes) {out.insert(out.end(),bytes.begin(),bytes.end());}
static std::vector<uint8_t> source(unsigned id3_version=3) {
    auto out=id3(id3_version,43);
    append(out,frame(0xfb,0x90,417));      // MPEG-1 Layer III, 128 kbps, 44.1 kHz.
    append(out,frame(0xfa,0xa2,523));      // VBR 160 kbps, padded, CRC present.
    append(out,frame(0xfb,0x90,417));
    return out;
}
static std::string field(const Tags& tags,const char* name) {
    for(const auto& item:tags.fields)if(item.first==name)return item.second;
    return {};
}
int main() {
    for(unsigned version:{3u,4u}) {
        auto bytes=source(version);Mp3Info info;std::string error;
        check(InspectMp3Source(bytes.data(),bytes.size(),info,error),"ID3v2.3/v2.4 source accepted");
        check(error.empty()&&info.rate==44100&&info.channels==2&&info.samples==3456&&
              info.frames==3&&info.audio_offset==53&&info.audio_size==1357,"literal MPEG-1 timing and audio range");
    }
    {
        auto bytes=frame(0xf3,0x80,208,0xc0); // MPEG-2 Layer III, 64 kbps, 22.05 kHz mono.
        append(bytes,frame(0xf3,0x82,209,0xc0));
        Mp3Info info;std::string error;
        check(InspectMp3Source(bytes.data(),bytes.size(),info,error)&&info.rate==22050&&
              info.channels==1&&info.samples==1152&&info.frames==2,"MPEG-2 mono padding");
    }
    {
        auto bytes=frame(0xe3,0x40,208); // MPEG-2.5 Layer III, 32 kbps, 11.025 kHz.
        Mp3Info info;std::string error;
        check(InspectMp3Source(bytes.data(),bytes.size(),info,error)&&info.rate==11025&&
              info.channels==2&&info.samples==576,"MPEG-2.5 timing");
    }
    {
        auto bytes=source();std::vector<uint8_t> audio(bytes.begin()+53,bytes.end());
        std::vector<uint8_t> id3v1(128,0);memcpy(id3v1.data(),"TAG",3);append(bytes,id3v1);
        Tags tags;tags.fields={{"TITLE","Episode"},{"ARTIST","Publisher"},{"ALBUM","Show"},
            {"SPOTIFY_URI","spotify:episode:0000000000000000000001"},
            {"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_CAPTURE","native compressed MP3 input"},
            {"HISTORY_MEDIA_DURATION","0.078367"},{"HISTORY_SAMPLES","3456"},
            {"CUSTOM","one"},{"CUSTOM","two"}};
        std::vector<uint8_t> owned;std::string error;
        check(MakeOwnedMp3(bytes.data(),bytes.size(),tags,owned,error),"owned MP3 construction");
        Mp3Info info;Tags inspected;
        check(InspectOwnedMp3(owned.data(),owned.size(),info,inspected,error),"owned MP3 inspection");
        check(field(inspected,"TITLE")=="Episode"&&field(inspected,"ARTIST")=="Publisher"&&
              field(inspected,"ALBUM")=="Show"&&field(inspected,"SPOTIFY_URI")=="spotify:episode:0000000000000000000001"&&
              field(inspected,"HISTORY_COMPLETE_LISTEN")=="1"&&field(inspected,"HISTORY_CAPTURE")=="native compressed MP3 input",
              "owned identity fields round trip");
        check(info.audio_size==audio.size()&&owned.size()>=audio.size()&&
              !memcmp(owned.data()+info.audio_offset,audio.data(),audio.size()),"every MP3 audio byte retained exactly");

        Tags rich=tags;for(unsigned i=0;i<24;++i)rich.fields.push_back({"FIELD_"+std::to_string(i),std::string(64,'a'+i%26)});
        rich.cover.assign(4096,0x5a);
        check(MakeOwnedMp3(bytes.data(),bytes.size(),rich,owned,error),"allocation fixture construction");
        bool rejected=false,completed=false;
        for(long fault=0;fault<128&&!completed;++fault) {
            Mp3Info fault_info;Tags partial;std::string fault_error;
            allocation_allowance=fault;const auto result=InspectOwnedMp3Result(owned.data(),owned.size(),fault_info,partial,fault_error);
            allocation_allowance=-1;
            const bool ok=result==Mp3InspectionResult::Valid;
            if(!ok) {
                rejected=true;
                check(result==Mp3InspectionResult::ResourceFailure,
                      "allocation failure remains distinguishable from corrupt MP3 input");
                check(partial.fields.empty()&&partial.cover.empty()&&!fault_info.frames,
                      "allocation failure never exposes partially trusted MP3 metadata");
            } else completed=true;
        }
        check(rejected&&completed,"every owned-MP3 parser allocation is contained before eventual success");

        auto changed=frame(0xfb,0x90,417);append(changed,frame(0xf3,0x80,208));
        Mp3Info failed_info;std::string failed_error;
        allocation_allowance=0;const bool failed=InspectMp3Source(changed.data(),changed.size(),failed_info,failed_error);
        allocation_allowance=-1;
        check(!failed&&!failed_info.frames,"error-message allocation failure is contained by the parser boundary");
    }
    const auto reject=[](std::vector<uint8_t> bytes,const char* message) {
        Mp3Info info;std::string error;check(!InspectMp3Source(bytes.data(),bytes.size(),info,error)&&!error.empty(),message);
    };
    auto truncated=source();truncated.pop_back();reject(std::move(truncated),"truncated frame rejected");
    auto junk=source();junk.insert(junk.begin()+53+417,0);reject(std::move(junk),"junk between frames rejected");
    reject(frame(0xeb,0x90,417),"reserved MPEG version rejected");
    reject(frame(0xfd,0x90,417),"non-Layer-III frame rejected");
    reject(frame(0xfb,0x00,417),"free bitrate rejected");
    reject(frame(0xfb,0xf0,417),"reserved bitrate rejected");
    reject(frame(0xfb,0x9c,417),"reserved sample rate rejected");
    auto changed=frame(0xfb,0x90,417);append(changed,frame(0xf3,0x80,208));reject(std::move(changed),"format change rejected");
    auto bad_id3=id3(4,0);bad_id3[6]=0x80;append(bad_id3,frame(0xfb,0x90,417));reject(std::move(bad_id3),"invalid synchsafe ID3 size rejected");
    std::puts("PASS: strict MP3 frame validation, ID3v2.3/v2.4 handling and exact owned audio retention");
}
