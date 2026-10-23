#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "lyrics_update.h"
#include "flac_history_core.h"
#include "history_settings.h"
#include "file_publication.h"
#include <memory>
namespace history {
namespace {
struct CloseFile {void operator()(void* h)const{if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
using File=std::unique_ptr<void,CloseFile>;
}

namespace {
bool CompleteOwnedFlac(const std::wstring& path,const Catalog& catalog) {
    RecordingFile recording;
    return ReadRecording(path,recording)&&recording.complete&&recording.owned&&
           recording.quality.codec==Codec::Flac&&MatchesRecording(recording,catalog);
}
bool ReadSource(HANDLE file,std::vector<uint8_t>& source) {
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file,&size)||size.QuadPart<=0||size.QuadPart>500LL*1024*1024)return false;
    source.resize(size_t(size.QuadPart));DWORD read=0;
    return ReadFile(file,source.data(),DWORD(source.size()),&read,nullptr)&&read==source.size();
}
}
void PreserveFlacLyrics(const std::wstring& path,Tags& incoming,const Catalog& catalog) {
    File file(CreateFileW(WindowsPath(path).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(file.get()==INVALID_HANDLE_VALUE||!CompleteOwnedFlac(path,catalog))return;
    std::vector<uint8_t> source;
    if(ReadSource(file.get(),source))CopyFlacLyrics(source,incoming);
}
FileLyricsUpdate UpdateFlacLyrics(const std::wstring& path,const Tags& incoming,const Catalog& catalog) {
    bool lyrics=false;
    for(const auto& field:incoming.fields)
        if(field.first=="LYRICS"&&field.second.find_first_not_of(" \t\r\n")!=std::string::npos)lyrics=true;
    if(!lyrics)return FileLyricsUpdate::Skipped;
    // This exact-object handle remains alive through guarded promotion. Other
    // processes cannot write/delete the snapshot or replace its original path.
    File file(CreateFileW(WindowsPath(path).c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(file.get()==INVALID_HANDLE_VALUE||!CompleteOwnedFlac(path,catalog))return FileLyricsUpdate::Failed;
    std::vector<uint8_t> source;
    if(!ReadSource(file.get(),source))return FileLyricsUpdate::Failed;
    std::vector<uint8_t> patched;std::string error;
    const auto result=FillFlacLyrics(source,incoming,patched,error);
    if(result!=LyricsUpdate::Updated)
        return result==LyricsUpdate::Unchanged?FileLyricsUpdate::Skipped:FileLyricsUpdate::Failed;
    const auto publication=PublishMetadataBytes(path,patched.data(),patched.size(),catalog,file.get());
    return publication==FilePublication::Upgraded?FileLyricsUpdate::Updated:FileLyricsUpdate::Failed;
}
}
