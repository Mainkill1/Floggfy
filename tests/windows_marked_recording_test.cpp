#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../native/media/aac_stream.h"
#include "../native/media/flac_history_core.h"
#include "../native/tooling/history_settings.h"
#include "../native/media/marked_recording.h"
#include "../native/media/metadata_update_windows.h"
#include "../native/media/mp3_stream.h"
#include "recording_fixture.h"
#include <cassert>
#include <cstdio>

using namespace history;
namespace {
using Bytes=std::vector<uint8_t>;
constexpr char uri[]="spotify:track:0123456789abcdefghijkl";
constexpr char creation_stamp[]="2016-02-29T03:04:05Z";
constexpr char other_stamp[]="2025-11-30T15:45:01Z";
constexpr const wchar_t* names[]={L"recording.flac",L"recording.ogg",L"recording.mp3",L"recording.m4a"};
constexpr const char* captures[]={"native compressed FLAC input","native compressed Ogg pages",
                                  "native compressed MP3 input","native compressed SpAC AAC packets"};
FILETIME Time(WORD year,WORD month,WORD day,WORD hour,WORD minute,WORD second) {
    SYSTEMTIME value{year,month,0,day,hour,minute,second,0};
    FILETIME result{};
    assert(SystemTimeToFileTime(&value,&result));
    return result;
}
void Write(const std::wstring& path,const Bytes& bytes) {
    const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);
    assert(file!=INVALID_HANDLE_VALUE);
    DWORD written=0;
    assert(WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr) && written==bytes.size());
    const auto creation=Time(2016,2,29,3,4,5);
    const auto modified=Time(2025,11,30,15,45,1);
    assert(SetFileTime(file,&creation,nullptr,&modified));
    assert(CloseHandle(file));
}
Bytes Encode(unsigned format) {
    Tags tags;
    tags.fields={{"SPOTIFY_URI",uri},{"HISTORY_COMPLETE_STREAM","1"},
                 {"HISTORY_CAPTURE",captures[format]},{"HISTORY_SAMPLES","3456"}};
    Bytes out;
    std::string error;
    if(format==0)assert(TagFlac(recording_fixture::fixture(),tags,out,error));
    if(format==1)assert(TagOgg(recording_fixture::ogg(),tags,out,error));
    if(format==2) {
        Bytes audio(417*3);
        for(size_t at=0;at<audio.size();at+=417) {
            audio[at]=0xff;audio[at+1]=0xfb;audio[at+2]=0x90;
        }
        assert(MakeOwnedMp3(audio.data(),audio.size(),tags,out,error));
    }
    if(format==3) {
        AacInfo info;
        info.rate=44100;info.channels=2;info.samples_per_packet=2048;
        info.priming=5119;info.total_samples=8192;info.config={0xeb,0x8a,0x08,0x00};
        const std::vector<Bytes> packets((info.total_samples+info.priming+2047)/2048,Bytes(4096,0xa5));
        assert(MakeAacM4a(info,packets,tags,out,error));
    }
    return out;
}
}
int main() {
    wchar_t module[MAX_PATH];
    const auto length=GetModuleFileNameW(nullptr,module,MAX_PATH);
    assert(length && length<MAX_PATH);
    const std::wstring executable(module,length);
    const auto root=executable.substr(0,executable.find_last_of(L'\\')+1);
    const auto settings=GetSettings();
    assert(settings.downloads && settings.metadata && settings.update_existing_metadata && settings.marked_when_downloaded);
    Catalog catalog;catalog.spotify_uri=uri;

    for(unsigned format=0;format<4;++format) {
        const auto path=root+names[format];
        const auto bytes=Encode(format);
        Write(path,bytes);
        const auto modified=Time(2025,11,30,15,45,1);
        const auto filetime=uint64_t(modified.dwHighDateTime)<<32|modified.dwLowDateTime;
        MarkedRecording mark;
        std::string error;
        assert(ReadMarkedRecording(path,mark,error) && mark.uri==uri && mark.complete && mark.owned);
        assert(mark.saved_utc.empty() && mark.bytes==bytes.size() && mark.write_time==filetime);
        assert(!BackfillMarkedDate(path,settings.generation+1));
        assert(BackfillMarkedDate(path,settings.generation));
        assert(ReadMarkedRecording(path,mark,error) && mark.saved_utc==creation_stamp);
        assert(!BackfillMarkedDate(path,settings.generation));

        Tags enrich;
        enrich.fields={{"HISTORY_SAVED_UTC",other_stamp},{"GENRE","Test genre"}};
        assert(UpdateRecordingMetadata(path,enrich,catalog,settings.generation)==
               (format==2?FileMetadataUpdate::Skipped:FileMetadataUpdate::Updated));
        assert(ReadMarkedRecording(path,mark,error) && mark.saved_utc==creation_stamp);
        assert(DeleteFileW(path.c_str()));
        std::wprintf(L"PASS: Windows %ls metadata read, exact creation time backfill and date preservation\n",names[format]);
    }
}
