// Exercise real hook event handling with downloads off: no complete compressed
// buffers are retained, and original parser return values remain unchanged.
#include "../native/audio_history.cpp"
#include <cassert>
#include <sstream>
#include "artwork_fixture.h"
namespace history {
std::string ReadClientPlaybackQuality(const Media&) {return {};}
void EnrichTags(const Media &, Tags & tags) { if(GetSettings().lyrics)tags.fields.push_back({"LYRICS","automatic lyric fill"}); }
} // namespace history
static int parser_calls = 0;
static int32_t Parser(void *, OggPage *) {
  ++parser_calls;
  return 58;
}
static void TestLyricsPublication() {
  wchar_t temp[2048];GetTempPathW(2048,temp);
  assert(SetSaveLocation(std::wstring(temp)+L"Floggfy-lyric-publication-"+std::to_wstring(GetCurrentProcessId())));
  assert(SetDownloads(true)&&SetFlac(true)&&SetOgg(true)&&SetLyrics(true));
  Heard heard;heard.media.title=L"Track";heard.media.artist=L"Artist";heard.media.album=L"Album";heard.media.track=1;
  auto settings=GetSettings();auto path=OutputPath(settings.root,CatalogFor(heard.media),L".flac",settings.music_folder);
  assert(EnsureDirectory(path.substr(0,path.find_last_of(L'\\'))));
  std::vector<uint8_t> source={'f','L','a','C',128,0,0,34};source.resize(42);source[8]=source[10]=16;
  uint64_t packed=(uint64_t(44100)<<44)|(uint64_t(1)<<41)|(uint64_t(23)<<36)|8192;
  for(unsigned i=0;i<8;i++)source[18+i]=uint8_t(packed>>(56-8*i));
  source.insert(source.end(),{255,248,1,2,3,4});
  Tags tags;tags.fields={{"TITLE","Track"},{"ARTIST","Artist"},{"ALBUM","Album"}};tags.cover=artwork_fixture::png();tags.mime="image/png";
  std::vector<uint8_t> existing;std::string error;assert(TagFlac(source,tags,existing,error));
  auto write=[&]{DeleteFileW(path.c_str());HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);assert(f!=INVALID_HANDLE_VALUE);DWORD n;assert(WriteFile(f,existing.data(),DWORD(existing.size()),&n,nullptr));CloseHandle(f);};
  Capture capture;capture.lossless=true;capture.flac={44100,2,16,4096,4096,8192};
  write();assert(Publish(capture,heard,CaptureEpoch())==Publication::Saved);
  assert(Publish(capture,heard,CaptureEpoch())==Publication::Skipped);
  Quality q;assert(ReadQuality(path,q)&&q.bits==24);
  // Lower-quality Vorbis playback can fill a better existing FLAC too.
  write();capture.lossless=false;assert(Publish(capture,heard,CaptureEpoch())==Publication::Saved);
  write();assert(SetLyrics(false));assert(Publish(capture,heard,CaptureEpoch())==Publication::Skipped);
  DeleteFileW(path.c_str());assert(SetDownloads(false));
}
static void WaitLog(const std::string& prefix,const std::string& suffix) {
  const auto path=WindowsPath(GetSettings().root+L"\\Floggfy.log");const auto deadline=GetTickCount64()+5000;
  while(GetTickCount64()<deadline){
    HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if(f!=INVALID_HANDLE_VALUE){
      DWORD size=GetFileSize(f,nullptr),n=0;std::string text;
      if(size!=INVALID_FILE_SIZE && size<=5*1024*1024){text.resize(size);if(!ReadFile(f,text.data(),size,&n,nullptr))text.clear();else text.resize(n);}
      CloseHandle(f);std::istringstream lines(text);std::string line;
      while(std::getline(lines,line))if(line.find(prefix)!=std::string::npos && line.find(suffix)!=std::string::npos)return;
    }
    Sleep(10);
  }
  std::fprintf(stderr,"Missing activity log: %s ... %s\n",prefix.c_str(),suffix.c_str());assert(false);
}
static void TestPublicationLogging() {
  assert(GetSettings().log && !GetSettings().debug_log);
  assert(SavedAudioDetails({Codec::Vorbis,44100,2,0,320000},1048576)=="Ogg Vorbis 320 kbps nominal, 44,100 Hz | 1.00 MiB (1048576 bytes)");
  assert(SavedAudioDetails({Codec::Vorbis,44100,2,0,0},1048576)=="Ogg Vorbis, 44,100 Hz | 1.00 MiB (1048576 bytes)");
  wchar_t temp[2048];GetTempPathW(2048,temp);
  assert(SetSaveLocation(std::wstring(temp)+L"Floggfy-publication-log-"+std::to_wstring(GetCurrentProcessId())));
  assert(SetDownloads(true)&&SetFlac(true)&&SetOgg(true)&&SetLyrics(false));
  Heard heard;heard.media.title=L"Log Track";heard.media.artist=L"Log Artist";heard.media.album=L"Log Album";heard.media.track=1;
  heard.media.cover=artwork_fixture::png();heard.media.cover_extension=L".png";
  const auto settings=GetSettings();const auto path=OutputPath(settings.root,CatalogFor(heard.media),L".flac",settings.music_folder);
  // Destination is available before publication, and remains the same for
  // existing files and quality upgrades. No save outcome gates this snapshot.
  PublishCurrentDestination(heard.media,settings);
  std::wstring song_folder;
  assert(ReadSongDestination(PlaybackIdentity(heard.media.Key()),settings.generation,song_folder));
  assert(song_folder==path.substr(0,path.find_last_of(L'\\')));
  auto make=[](unsigned rate,unsigned bits){
    auto c=std::make_unique<Capture>();c->lossless=true;c->flac={rate,2,bits,4096,4096,8192};c->coverage.samples=8192;c->coverage.frames=2;
    std::vector<uint8_t> bytes={'f','L','a','C',128,0,0,34};bytes.resize(42);bytes[8]=bytes[10]=16;
    const uint64_t packed=(uint64_t(rate)<<44)|(uint64_t(1)<<41)|(uint64_t(bits-1)<<36)|8192;
    for(unsigned i=0;i<8;i++)bytes[18+i]=uint8_t(packed>>(56-8*i));
    bytes.insert(bytes.end(),{255,248,1,2,3,4});
    c->bytes=bytes.size();assert(c->data.Append(bytes.data(),bytes.size(),65536));return c;
  };
  auto capture=make(44100,16);assert(Publish(*capture,heard,CaptureEpoch())==Publication::Saved);
  WIN32_FILE_ATTRIBUTE_DATA attributes{};assert(GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&attributes));
  auto size=std::to_string((uint64_t(attributes.nFileSizeHigh)<<32)|attributes.nFileSizeLow);
  WaitLog("finished FLAC 16-bit, 44,100 Hz", "("+size+" bytes) | Log Artist - Log Track");
  capture=make(44100,24);assert(Publish(*capture,heard,CaptureEpoch())==Publication::Saved);
  assert(GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&attributes));size=std::to_string((uint64_t(attributes.nFileSizeHigh)<<32)|attributes.nFileSizeLow);
  WaitLog("upgraded FLAC 24-bit, 44,100 Hz", "("+size+" bytes) | Log Artist - Log Track");
  std::vector<RecentDownload> recent;assert(ReadRecentDownloads(recent)&&recent.size()==1);
  assert(recent[0].path==path && recent[0].title==L"Log Artist - Log Track" && recent[0].bytes==std::stoull(size));
  capture=make(44100,24);assert(Publish(*capture,heard,CaptureEpoch())==Publication::Skipped);
  WaitLog("skipped same quality already exists on disk", "Log Artist - Log Track");
  capture=make(44100,16);assert(Publish(*capture,heard,CaptureEpoch())==Publication::Skipped);
  WaitLog("skipped better quality already exists on disk", "Log Artist - Log Track");
  capture=make(48000,16);assert(Publish(*capture,heard,CaptureEpoch())==Publication::Skipped);
  WaitLog("skipped existing file retained; incoming audio is not a verified quality upgrade", "Log Artist - Log Track");
  assert(DeleteFileW(path.c_str()));
  assert(EnsureDirectory(path.substr(0,path.find_last_of(L'\\'))));
  HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);DWORD n;
  assert(f!=INVALID_HANDLE_VALUE&&WriteFile(f,"unknown",7,&n,nullptr));CloseHandle(f);
  capture=make(44100,24);assert(Publish(*capture,heard,CaptureEpoch())==Publication::Skipped);
  WaitLog("skipped existing file quality could not be verified; retained", "Log Artist - Log Track");
  assert(DeleteFileW(path.c_str()));
  assert(SetDownloads(false));assert(Publish(*capture,heard,CaptureEpoch())==Publication::Skipped);
  WaitLog("skipped capture settings changed or downloads/format disabled", "Log Artist - Log Track");
  assert(SetDownloads(true));capture=make(44100,24);capture->data.Flatten();
  assert(Publish(*capture,heard,CaptureEpoch())==Publication::Failed);
  WaitLog("failed tagging failed:","Log Artist - Log Track");
  assert(ReadRecentDownloads(recent)&&recent.size()==1); // Failures/settings/skips never add entries.
  assert(SetDownloads(false));
  std::puts("PASS: normal publication logs contain final quality/size and accurate same/better/unverified/settings skip reasons");
}
int main() {
  wchar_t ini_path[2048];GetModuleFileNameW(nullptr,ini_path,2048);std::wstring ini=ini_path;ini.resize(ini.find_last_of(L'\\')+1);ini+=L"SpotifyHistory.ini";
  assert(WritePrivateProfileStringW(L"To Disk",L"Log",L"1",ini.c_str()));
  assert(WritePrivateProfileStringW(L"To Disk",L"DebugLog",L"0",ini.c_str()));
  InitSettings(nullptr);
  std::vector<RecentDownload> recent;assert(ReadRecentDownloads(recent)&&recent.empty());
  TestLyricsPublication();
  assert(ReadRecentDownloads(recent)&&recent.empty()); // Lyrics-only updates and skips are not downloads.
  TestPublicationLogging();
  assert(SetDownloads(false)); // Explicit fixture preference, independent of install defaults.
  assert(!GetSettings().downloads);
  queue =
      static_cast<Slot *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Slot) * capacity));
  assert(queue);
  event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  assert(event);
  quality_enabled = true;
  std::vector<uint8_t> data(70000);
  FlacEvent(reinterpret_cast<void *>(2), 3, data.data(), data.size());
  Slot first, second;
  assert(Pop(first) && Pop(second) && !Pop(first));
  assert(!first.capture && !second.capture && first.length == 42 && second.length == 42);
  assert(first.input_length + second.input_length == 70000);
  unsigned char header[28] = {}, body[30] = {};
  OggPage page{header, 28, body, 30};
  original = Parser;
  assert(Hook(reinterpret_cast<void *>(1), &page) == 58 && parser_calls == 1);
  Slot observed;
  assert(Pop(observed) && observed.kind == 0 && !observed.capture && observed.length == 58);
  quality_enabled = false;
  FlacEvent(reinterpret_cast<void *>(2), 3, data.data(), data.size());
  assert(Hook(reinterpret_cast<void *>(1), &page) == 58 && parser_calls == 2 && !Pop(observed));
  CloseHandle(event);
  event = nullptr;
  HeapFree(GetProcessHeap(), 0, queue);
  queue = nullptr;
  std::puts("PASS: downloads-off observation, bounded FLAC prefix copy, complete byte accounting "
            "and unchanged original decoder returns");
}
