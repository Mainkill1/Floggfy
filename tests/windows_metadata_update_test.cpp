#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../native/metadata_update_windows.h"
#include "../native/file_publication.h"
#include "../native/flac_history_core.h"
#include "../native/history_settings.h"
#include "recording_fixture.h"
#include "artwork_fixture.h"
#include <cassert>
#include <cstdio>
using namespace history;
static bool race=false;
static void Write(const std::wstring& path,const std::vector<uint8_t>& bytes) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);assert(file!=INVALID_HANDLE_VALUE);
    DWORD written=0;assert(WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size());CloseHandle(file);
}
static std::string Field(const Tags& tags,const char* key) {
    for(const auto& field:tags.fields)if(field.first==key)return field.second;
    return {};
}
namespace history {
void PublicationTestPhase(const wchar_t* phase,const std::wstring& path) {
    if(race&&std::wstring(phase)==L"reserved")Write(path,{1,2,3});
}
}
int main() {
    wchar_t temp[MAX_PATH];assert(GetTempPathW(MAX_PATH,temp));
    const auto root=std::wstring(temp)+L"Floggfy-metadata-fill-"+std::to_wstring(GetCurrentProcessId());
    assert(CreateDirectoryW(root.c_str(),nullptr));
    Catalog catalog;catalog.title=L"Track";catalog.album=L"Album";catalog.artist=L"Artist";catalog.spotify_uri="spotify:track:one";
    Tags old;old.fields={{"TITLE","Track"},{"ARTIST","Artist"},{"ALBUM","Album"},{"SPOTIFY_URI",catalog.spotify_uri},
        {"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_CAPTURE","native compressed FLAC input"},{"COMMENT","keep"}};
    Tags incoming;incoming.fields={{"GENRE","Rock"},{"YEAR","2021"},{"COMMENT","overwrite forbidden"},{"HISTORY_BITS_PER_SAMPLE","16"}};
    incoming.cover=artwork_fixture::png();incoming.mime="image/png";
    std::string error;std::vector<uint8_t> bytes;
    for(bool flac:{true,false}) {
        old.fields[5].second=flac?"native compressed FLAC input":"native compressed Ogg pages";
        assert(flac?TagFlac(recording_fixture::fixture(44100,24),old,bytes,error):TagOgg(recording_fixture::ogg(),old,bytes,error));
        const auto path=root+(flac?L"\\Track.flac":L"\\Track.ogg");Write(path,bytes);
        assert(UpdateRecordingMetadata(path,incoming,catalog)==FileMetadataUpdate::Updated);
        RecordingFile result;assert(ReadRecording(path,result)&&result.complete&&result.owned&&result.has_artwork&&MatchesRecording(result,catalog));
        assert(Field(result.tags,"GENRE")=="Rock"&&Field(result.tags,"YEAR")=="2021"&&Field(result.tags,"COMMENT")=="keep");
        assert(Field(result.tags,"HISTORY_BITS_PER_SAMPLE").empty());
        if(flac)assert(result.quality.bits==24);
        assert(UpdateRecordingMetadata(path,incoming,catalog)==FileMetadataUpdate::Skipped);
        auto wrong=catalog;wrong.spotify_uri="spotify:track:other";
        assert(UpdateRecordingMetadata(path,incoming,wrong)==FileMetadataUpdate::Failed);
        assert(DeleteFileW(path.c_str()));
        Write(path,bytes);race=true;
        assert(UpdateRecordingMetadata(path,incoming,catalog)==FileMetadataUpdate::Failed);race=false;
        const auto recovered=ResolveRecordingPath(path,catalog);assert(recovered!=path&&ReadRecording(recovered,result)&&result.complete);
        HANDLE foreign=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);assert(foreign!=INVALID_HANDLE_VALUE&&GetFileSize(foreign,nullptr)==3);CloseHandle(foreign);
        assert(DeleteFileW(path.c_str())&&DeleteFileW(recovered.c_str()));
    }
    assert(RemoveDirectoryW(root.c_str()));
    std::puts("PASS: existing FLAC/Ogg fill missing metadata and artwork without replacing values/audio; duplicate, identity and racing-file protection");
}
