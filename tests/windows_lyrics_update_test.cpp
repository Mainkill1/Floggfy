#include "../native/lyrics_update.h"
#include "../native/flac_history_core.h"
#include "artwork_fixture.h"
#include <windows.h>
#include <cassert>
#include <cstdio>
using namespace history;
static std::vector<uint8_t> Read(const std::wstring& path){HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);assert(f!=INVALID_HANDLE_VALUE);std::vector<uint8_t> b(GetFileSize(f,nullptr));DWORD n;assert(ReadFile(f,b.data(),DWORD(b.size()),&n,nullptr)&&n==b.size());CloseHandle(f);return b;}
int main(){
 wchar_t root[2048];GetTempPathW(2048,root);const auto path=std::wstring(root)+L"floggfy-lyrics-"+std::to_wstring(GetCurrentProcessId())+L".flac";
 std::vector<uint8_t> source={'f','L','a','C',128,0,0,34};source.resize(42);source[8]=source[10]=16;
 uint64_t packed=(uint64_t(44100)<<44)|(uint64_t(1)<<41)|(uint64_t(23)<<36)|8192;for(unsigned i=0;i<8;i++)source[18+i]=uint8_t(packed>>(56-8*i));source.insert(source.end(),{255,248,1,2,3,4});
 Tags tags;tags.fields={{"TITLE","Track"},{"ARTIST","Artist"},{"ALBUM","Album"},{"COMMENT","keep this"}};tags.cover=artwork_fixture::png();tags.mime="image/png";
 std::vector<uint8_t> original;std::string error;assert(TagFlac(source,tags,original,error));
 HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);assert(f!=INVALID_HANDLE_VALUE);DWORD n;assert(WriteFile(f,original.data(),DWORD(original.size()),&n,nullptr)&&n==original.size());CloseHandle(f);
 tags.cover.clear();tags.fields.push_back({"LYRICS","中文\nÉté"});
 assert(UpdateFlacLyrics(path,tags)==FileLyricsUpdate::Updated);
 auto updated=Read(path);Tags actual;assert(ReadFlacComments(updated,actual,error));assert(actual.fields.back().second=="中文\nÉté");
 FlacInfo a,b;size_t x,y;assert(ParseFlacMetadata(original,a,x)==FlacParse::Valid&&ParseFlacMetadata(updated,b,y)==FlacParse::Valid);
 assert(b.bits==24&&std::vector<uint8_t>(original.begin()+x,original.end())==std::vector<uint8_t>(updated.begin()+y,updated.end()));
 assert(UpdateFlacLyrics(path,tags)==FileLyricsUpdate::Skipped&&Read(path)==updated);
 DeleteFileW(path.c_str());
 // A reader denying writes/deletes prevents modification, preserving the file.
 f=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);assert(f!=INVALID_HANDLE_VALUE);assert(WriteFile(f,original.data(),DWORD(original.size()),&n,nullptr));CloseHandle(f);
 f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);assert(f!=INVALID_HANDLE_VALUE);
 assert(UpdateFlacLyrics(path,tags)==FileLyricsUpdate::Failed);CloseHandle(f);assert(Read(path)==original);
 DeleteFileW(path.c_str());puts("PASS: atomic lyric fill preserves existing 24-bit audio, artwork/tags, existing lyrics and locked files");
}
