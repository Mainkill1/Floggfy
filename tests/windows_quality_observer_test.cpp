// Exercise real hook event handling with downloads off: no complete compressed
// buffers are retained, and original parser return values remain unchanged.
#include "../native/audio_history.cpp"
#include <cassert>
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
int main() {
  InitSettings(nullptr);
  TestLyricsPublication();
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
