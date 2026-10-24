#include "../native/lyrics_update.h"
#include "../native/flac_history_core.h"
#include "../native/existing_quality.h"
#include "../native/history_settings.h"
#include "recording_fixture.h"
#include "artwork_fixture.h"
#include <windows.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
using namespace history;
namespace {
std::wstring mode;
bool staged_write_denied=false,staged_delete_denied=false,foreign_created=false;
const std::vector<uint8_t> foreign={'f','o','r','e','i','g','n'};
void Write(const std::wstring& path,const std::vector<uint8_t>& bytes) {
 HANDLE file=CreateFileW(WindowsPath(path).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);assert(file!=INVALID_HANDLE_VALUE);
 DWORD written=0;assert(WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size());CloseHandle(file);
}
std::vector<uint8_t> Read(const std::wstring& path) {
 HANDLE file=CreateFileW(WindowsPath(path).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);assert(file!=INVALID_HANDLE_VALUE);
 const auto size=GetFileSize(file,nullptr);assert(size!=INVALID_FILE_SIZE);
 std::vector<uint8_t> bytes(size);DWORD read=0;assert(ReadFile(file,bytes.data(),DWORD(bytes.size()),&read,nullptr)&&read==bytes.size());CloseHandle(file);return bytes;
}
std::string Field(const Tags& tags,const char* key) {
 const auto found=std::find_if(tags.fields.begin(),tags.fields.end(),[&](const auto& field){return field.first==key;});return found==tags.fields.end()?std::string():found->second;
}
std::vector<uint8_t> NonCommentBlocks(const std::vector<uint8_t>& bytes) {
 FlacInfo info;size_t audio=0;assert(ParseFlacMetadata(bytes,info,audio)==FlacParse::Valid);
 std::vector<uint8_t> blocks;
 for(size_t p=4;p<audio;){const auto size=(size_t(bytes[p+1])<<16)|(size_t(bytes[p+2])<<8)|bytes[p+3];if((bytes[p]&127)!=4)blocks.insert(blocks.end(),bytes.begin()+p,bytes.begin()+p+4+size);p+=4+size;}
 return blocks;
}
}
namespace history {
void PublicationTestPhase(const wchar_t* phase,const std::wstring& path) {
 if(mode!=L"race")return;
 if(std::wstring(phase)==L"staged") {
  HANDLE write=CreateFileW(WindowsPath(path).c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
  staged_write_denied=write==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_SHARING_VIOLATION;
  if(write!=INVALID_HANDLE_VALUE)CloseHandle(write);
  HANDLE remove=CreateFileW(WindowsPath(path).c_str(),DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
  staged_delete_denied=remove==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_SHARING_VIOLATION;
  if(remove!=INVALID_HANDLE_VALUE)CloseHandle(remove);
 }
 if(std::wstring(phase)==L"reserved") {Write(path,foreign);foreign_created=true;}
}
}
int main() {
 wchar_t temporary[MAX_PATH];assert(GetTempPathW(MAX_PATH,temporary));
 const auto root=std::wstring(temporary)+L"Floggfy-lyrics-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
 assert(CreateDirectoryW(root.c_str(),nullptr));const auto path=root+L"\\Track.flac";
 Catalog catalog;catalog.title=L"Track";catalog.artist=L"Artist";catalog.album=L"Album";catalog.track=1;catalog.disc=1;catalog.spotify_uri="spotify:track:0000000000000000000001";
 Tags tags;tags.fields={{"TITLE","Track"},{"ARTIST","Artist"},{"ALBUM","Album"},{"TRACKNUMBER","1"},{"DISCNUMBER","1"},{"SPOTIFY_URI",catalog.spotify_uri},
                      {"COMMENT","keep this"},{"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_CAPTURE","native compressed FLAC input"}};
 tags.cover=artwork_fixture::png();tags.mime="image/png";
 std::vector<uint8_t> original;std::string error;assert(TagFlac(recording_fixture::fixture(44100,24),tags,original,error));
 RecordingFile checked;assert(InspectRecording(original.data(),original.size(),checked)&&checked.complete&&checked.owned&&MatchesRecording(checked,catalog));
 auto reset=[&](const std::vector<uint8_t>& bytes){DeleteFileW(WindowsPath(path).c_str());Write(path,bytes);};
 reset(original);auto incoming=tags;incoming.cover.clear();incoming.fields.push_back({"LYRICS","中文\nÉté"});
 assert(UpdateFlacLyrics(path,incoming,catalog)==FileLyricsUpdate::Updated);
 auto updated=Read(path);Tags actual;assert(ReadFlacComments(updated,actual,error));assert(Field(actual,"LYRICS")=="中文\nÉté"&&Field(actual,"COMMENT")=="keep this");
 FlacInfo a,b;size_t x=0,y=0;assert(ParseFlacMetadata(original,a,x)==FlacParse::Valid&&ParseFlacMetadata(updated,b,y)==FlacParse::Valid);
 assert(b.bits==24&&std::vector<uint8_t>(original.begin()+x,original.end())==std::vector<uint8_t>(updated.begin()+y,updated.end()));
 assert(NonCommentBlocks(original)==NonCommentBlocks(updated));
 assert(UpdateFlacLyrics(path,incoming,catalog)==FileLyricsUpdate::Skipped&&Read(path)==updated);
 incoming.fields.back().second="[00:00.000]中文\n[00:00.001]Été";
 assert(UpdateFlacLyrics(path,incoming,catalog)==FileLyricsUpdate::Updated);updated=Read(path);
 assert(NonCommentBlocks(original)==NonCommentBlocks(updated));
 auto upgrade=tags;upgrade.cover.clear();PreserveFlacLyrics(path,upgrade,catalog);assert(Field(upgrade,"LYRICS")==incoming.fields.back().second);
 auto wrong=catalog;wrong.spotify_uri="spotify:track:0000000000000000000002";
 upgrade=tags;PreserveFlacLyrics(path,upgrade,wrong);assert(Field(upgrade,"LYRICS").empty());
 reset(original);assert(UpdateFlacLyrics(path,incoming,wrong)==FileLyricsUpdate::Failed&&Read(path)==original);
 // Existing readers that deny delete sharing prevent even metadata promotion.
 HANDLE held=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);assert(held!=INVALID_HANDLE_VALUE);
 assert(UpdateFlacLyrics(path,incoming,catalog)==FileLyricsUpdate::Failed);CloseHandle(held);assert(Read(path)==original);
 // Never patch or carry lyrics from unowned, incomplete or unrelated files.
 auto unowned_tags=incoming;unowned_tags.fields.erase(std::remove_if(unowned_tags.fields.begin(),unowned_tags.fields.end(),[](const auto& field){return field.first=="HISTORY_COMPLETE_LISTEN"||field.first=="HISTORY_CAPTURE";}),unowned_tags.fields.end());
 std::vector<uint8_t> unowned;assert(TagFlac(recording_fixture::fixture(44100,24),unowned_tags,unowned,error));reset(unowned);
 assert(UpdateFlacLyrics(path,incoming,catalog)==FileLyricsUpdate::Failed&&Read(path)==unowned);
 upgrade=tags;PreserveFlacLyrics(path,upgrade,catalog);assert(Field(upgrade,"LYRICS").empty());
 auto partial=updated;partial.pop_back();reset(partial);
 assert(UpdateFlacLyrics(path,incoming,catalog)==FileLyricsUpdate::Failed&&Read(path)==partial);
 upgrade=tags;PreserveFlacLyrics(path,upgrade,catalog);assert(Field(upgrade,"LYRICS").empty());
 // The old object remains locked through staging. A foreign file appearing
 // after reservation survives; the original recording remains recoverable.
 reset(original);mode=L"race";
 assert(UpdateFlacLyrics(path,incoming,catalog)==FileLyricsUpdate::Failed);mode.clear();
 assert(staged_write_denied&&staged_delete_denied&&foreign_created&&Read(path)==foreign);
 assert(Read(path+L".previous")==original);
 const auto recovered=ResolveRecordingPath(path,catalog);
 assert(!recovered.empty()&&recovered!=path&&Read(path)==foreign&&Read(recovered)==original);
 assert(ReadRecording(recovered,checked)&&checked.complete&&checked.owned&&checked.quality.bits==24&&MatchesRecording(checked,catalog));
 assert(DeleteFileW(path.c_str())&&DeleteFileW(recovered.c_str()));assert(RemoveDirectoryW(root.c_str()));
 puts("PASS: owned complete same-recording lyric updates preserve 24-bit audio/art/tags, reject locks/unverified sources and retain foreign raced targets with recoverable originals");
}
