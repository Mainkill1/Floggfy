// Exercise production ReadRecording with a failing Win32 storage boundary.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cassert>
#include <new>
#include <vector>
#include <string>
#include "recording_fixture.h"
#include "../native/flac_history_core.h"
#include "../native/history_settings.h"
#include "../native/mp3_stream.h"
#include "../native/recording_limits.h"
namespace {
enum class Failure {None,ReadError,ShortRead,Oversize,SizeChanged,Allocation};
Failure failure=Failure::None;
unsigned read_calls=0,size_calls=0;
thread_local bool fail_allocation=false;
thread_local long allocation_allowance=-1;
thread_local bool allocation_failed=false;
[[maybe_unused]] BOOL WINAPI CheckedRead(HANDLE file,LPVOID bytes,DWORD wanted,LPDWORD got,LPOVERLAPPED overlapped) {
 const auto call=read_calls++;
 if(call && failure==Failure::ReadError){*got=0;SetLastError(ERROR_READ_FAULT);return FALSE;}
 if(call && failure==Failure::ShortRead)return ReadFile(file,bytes,wanted-1,got,overlapped);
 return ReadFile(file,bytes,wanted,got,overlapped);
}
BOOL WINAPI CheckedSize(HANDLE file,PLARGE_INTEGER size) {
 if(!GetFileSizeEx(file,size))return FALSE;
 const auto call=size_calls++;
 if(failure==Failure::Oversize)size->QuadPart=static_cast<LONGLONG>(history::kMaximumRecordingBytes)+1;
 if(failure==Failure::SizeChanged&&call)++size->QuadPart;
 if(failure==Failure::Allocation&&!call)fail_allocation=true;
 return TRUE;
}
}
void* operator new(std::size_t size){
 if(fail_allocation){fail_allocation=false;allocation_failed=true;throw std::bad_alloc();}
 if(allocation_allowance==0){allocation_failed=true;throw std::bad_alloc();}
 if(allocation_allowance>0)--allocation_allowance;
 if(auto p=HeapAlloc(GetProcessHeap(),0,size?size:1))return p;
 throw std::bad_alloc();
}
void operator delete(void* p) noexcept {if(p)HeapFree(GetProcessHeap(),0,p);}
void operator delete(void* p,std::size_t) noexcept {if(p)HeapFree(GetProcessHeap(),0,p);}
#define ReadFile CheckedRead
#define GetFileSizeEx CheckedSize
#include "../native/existing_quality.cpp"
#undef ReadFile
#undef GetFileSizeEx
static void Reset(history::RecordingFile& recording) {
 recording.complete=true;recording.owned=true;recording.quality.bits=24;recording.quality.rate=44100;
 recording.tags.fields={{"TITLE","stale output"}};recording.tags.cover={1,2,3};recording.tags.mime="stale";
 read_calls=size_calls=0;
}
static void Empty(const history::RecordingFile& recording) {
 assert(!recording.complete&&!recording.owned&&!recording.quality.bits&&!recording.quality.rate);
 assert(recording.tags.fields.empty()&&recording.tags.cover.empty()&&recording.tags.mime.empty());
}
int main() {
 using namespace history;
 Catalog catalog;catalog.title=L"Track";catalog.album=L"Album";catalog.artist=L"Artist";catalog.spotify_uri="spotify:track:0000000000000000000001";
 Tags tags;tags.fields={{"TITLE","Track"},{"ARTIST","Artist"},{"ALBUM","Album"},{"SPOTIFY_URI",catalog.spotify_uri},{"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_CAPTURE","native compressed FLAC input"}};
 std::vector<uint8_t> bytes;std::string error;assert(TagFlac(recording_fixture::fixture(44100,24),tags,bytes,error));
 // A valid two-MiB padding block requires multiple production reads without
 // changing any audio frames, and avoids a huge artificial audio fixture.
 FlacInfo info;size_t audio=0;assert(ParseFlacMetadata(bytes,info,audio)==FlacParse::Valid);
 for(size_t p=4;p<audio;){const auto n=(size_t(bytes[p+1])<<16)|(size_t(bytes[p+2])<<8)|bytes[p+3];bytes[p]&=127;p+=4+n;}
 const size_t padding=2*1024*1024;
 bytes.insert(bytes.begin()+audio,{129,uint8_t(padding>>16),uint8_t(padding>>8),uint8_t(padding)});
 bytes.insert(bytes.begin()+audio+4,padding,0);
 wchar_t temporary[MAX_PATH];assert(GetTempPathW(MAX_PATH,temporary));
 const auto path=std::wstring(temporary)+L"Floggfy-recording-read-"+std::to_wstring(GetCurrentProcessId())+L".flac";
 HANDLE output=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);assert(output!=INVALID_HANDLE_VALUE);DWORD written=0;
 assert(WriteFile(output,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size());CloseHandle(output);
 RecordingFile recording;assert(ReadRecording(path,recording)&&recording.complete&&recording.owned&&recording.quality.bits==24&&MatchesRecording(recording,catalog));
 for(const auto kind:{Failure::ReadError,Failure::ShortRead,Failure::Oversize,Failure::SizeChanged,Failure::Allocation}) {
  Reset(recording);failure=kind;
  assert(!ReadRecording(path,recording));Empty(recording);
  if(kind==Failure::ReadError||kind==Failure::ShortRead)assert(read_calls==2);
  if(kind==Failure::Oversize)assert(read_calls==0);
 }
 failure=Failure::None;Reset(recording);
 assert(ReadRecording(path,recording)&&recording.complete&&recording.owned&&MatchesRecording(recording,catalog)&&read_calls>=3&&size_calls==2);

 // Parser resource exhaustion is an unreadable-file result, never corruption
 // that permits a collision-suffixed duplicate beside a valid podcast.
 std::vector<uint8_t> mp3_source(417*3);
 for(size_t at=0;at<mp3_source.size();at+=417){mp3_source[at]=0xff;mp3_source[at+1]=0xfb;mp3_source[at+2]=0x90;}
 Tags mp3_tags;mp3_tags.fields={{"TITLE","Episode"},{"ARTIST","Publisher"},{"ALBUM","Show"},
  {"SPOTIFY_URI","spotify:episode:0000000000000000000002"},{"HISTORY_COMPLETE_LISTEN","1"},
  {"HISTORY_CAPTURE","native compressed MP3 input"},{"HISTORY_SAMPLES","3456"}};
 std::vector<uint8_t> owned_mp3;assert(MakeOwnedMp3(mp3_source.data(),mp3_source.size(),mp3_tags,owned_mp3,error));
 const auto mp3_path=std::wstring(temporary)+L"Floggfy-recording-read-"+std::to_wstring(GetCurrentProcessId())+L".mp3";
 output=CreateFileW(mp3_path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);assert(output!=INVALID_HANDLE_VALUE);
 assert(WriteFile(output,owned_mp3.data(),DWORD(owned_mp3.size()),&written,nullptr)&&written==owned_mp3.size());CloseHandle(output);
 bool parser_succeeded=false,parser_faulted=false;
 for(long fault=0;fault<128&&!parser_succeeded;++fault) {
  RecordingFile inspected;allocation_allowance=fault;allocation_failed=false;
  const auto result=InspectRecordingFile(mp3_path,inspected);
  allocation_allowance=-1;
  if(allocation_failed){parser_faulted=true;assert(result==RecordingRead::ReadError);}
  else {assert(result==RecordingRead::ValidComplete);parser_succeeded=true;}
 }
 assert(parser_faulted&&parser_succeeded&&DeleteFileW(mp3_path.c_str()));
 // Every failure path must close the source handle, permitting later writes.
 output=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);assert(output!=INVALID_HANDLE_VALUE);CloseHandle(output);
 assert(DeleteFileW(path.c_str()));Reset(recording);assert(!ReadRecording(path,recording));Empty(recording);
 puts("PASS: checked bounded recording reads reject storage faults, short/changed/oversize files and allocation failure without stale output or leaked handles");
}
