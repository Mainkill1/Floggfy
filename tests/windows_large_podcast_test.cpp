// Optional real-file test: requires about 1.1 GiB RAM and 513 MiB temporary disk.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../native/mp3_stream.h"
#include "../native/file_publication.h"
#include <cassert>
#include <cstdio>
namespace history {std::wstring WindowsPath(const std::wstring& path){return path;}}
int main() {
    using namespace history;
    // Structurally valid 320 kbps MP3 frames, just above the old 512 MiB cutoff.
    constexpr size_t frame_size=1044,frames=(512u*1024u*1024u)/frame_size+1;
    std::vector<uint8_t> source(frames*frame_size);
    for(size_t at=0;at<source.size();at+=frame_size){source[at]=0xff;source[at+1]=0xfb;source[at+2]=0xe0;}
    Catalog catalog;catalog.kind=MediaKind::Podcast;catalog.title=L"Long podcast";catalog.track=1;
    catalog.spotify_uri="spotify:episode:AAAAAAAAAAAAAAAAAAAAAA";
    Tags tags;tags.fields={{"TITLE","Long podcast"},{"TRACKNUMBER","1"},{"SPOTIFY_URI",catalog.spotify_uri},
        {"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_CAPTURE","native compressed MP3 input"},
        {"HISTORY_SAMPLES",std::to_string(uint64_t(frames)*1152)}};
    std::vector<uint8_t> owned;std::string error;
    assert(MakeOwnedMp3(source.data(),source.size(),tags,owned,error));
    source.clear();source.shrink_to_fit();
    RecordingFile recording;assert(InspectRecording(owned.data(),owned.size(),recording)&&recording.complete&&recording.owned);
    assert(owned.size()>512u*1024u*1024u&&MatchesRecording(recording,catalog));
    wchar_t temporary[MAX_PATH];assert(GetTempPathW(MAX_PATH,temporary));
    const auto root=std::wstring(temporary)+L"Floggfy-large-podcast-"+std::to_wstring(GetCurrentProcessId());
    assert(CreateDirectoryW(root.c_str(),nullptr));
    const auto numbered=root+L"\\01 - Long podcast.mp3",title=root+L"\\Long podcast.mp3";
    size_t published=0;const auto quality=recording.quality;
    assert(PublishBytes(numbered,quality,owned.data(),owned.size(),catalog,&published)==FilePublication::Saved);
    assert(published==owned.size());
    Quality existing;assert(ExistingRecording(numbered,catalog,existing)&&existing.codec==Codec::Mp3);
    std::wstring renamed;
    assert(ConvertRecordingFilename(numbered,title,catalog,renamed)==FilenameConversion::Renamed&&renamed==title);
    assert(PublishBytes(title,quality,owned.data(),owned.size(),catalog)==FilePublication::Skipped);
    assert(ExistingRecording(title,catalog,existing));
    assert(DeleteFileW(title.c_str())&&RemoveDirectoryW(root.c_str()));
    printf("PASS: %zu-byte podcast tagged, published, read, renamed and reused on Windows\n",owned.size());
}
