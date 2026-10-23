#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "lyrics_update.h"
#include "flac_history_core.h"
#include "history_settings.h"
#include <memory>
namespace history {
namespace {
struct CloseFile {void operator()(void* h)const{if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
using File=std::unique_ptr<void,CloseFile>;
}

void PreserveFlacLyrics(const std::wstring& path,Tags& incoming) {
    File file(CreateFileW(WindowsPath(path).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(file.get()==INVALID_HANDLE_VALUE)return;
    LARGE_INTEGER size{};std::vector<uint8_t> source;
    if(GetFileSizeEx(file.get(),&size)&&size.QuadPart>0&&size.QuadPart<=500LL*1024*1024){source.resize(size_t(size.QuadPart));DWORD n=0;if(!ReadFile(file.get(),source.data(),DWORD(source.size()),&n,nullptr)||n!=source.size())source.clear();}
    file.reset();if(!source.empty())CopyFlacLyrics(source,incoming);
}
FileLyricsUpdate UpdateFlacLyrics(const std::wstring& path,const Tags& incoming) {
    bool lyrics=false;for(const auto& field:incoming.fields)if(field.first=="LYRICS" && field.second.find_first_not_of(" \t\r\n")!=std::string::npos)lyrics=true;
    if(!lyrics)return FileLyricsUpdate::Skipped;
    const auto name=WindowsPath(path);
    // Deny writes/deletes while reading and staging so a concurrent writer
    // cannot change the snapshot used to build the metadata-only replacement.
    File file(CreateFileW(name.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(file.get()==INVALID_HANDLE_VALUE)return FileLyricsUpdate::Failed;
    LARGE_INTEGER size{};bool ok=GetFileSizeEx(file.get(),&size)&&size.QuadPart>0&&size.QuadPart<=500LL*1024*1024;
    std::vector<uint8_t> source;if(ok){source.resize(size_t(size.QuadPart));DWORD read=0;ok=ReadFile(file.get(),source.data(),DWORD(source.size()),&read,nullptr)&&read==source.size();}
    std::vector<uint8_t> patched;std::string error;
    const auto result=ok?FillFlacLyrics(source,incoming,patched,error):LyricsUpdate::Invalid;
    if(result!=LyricsUpdate::Updated){file.reset();return result==LyricsUpdate::Unchanged?FileLyricsUpdate::Skipped:FileLyricsUpdate::Failed;}
    static volatile LONG sequence=0;
    const auto staging=WindowsPath(path+L".complete-lyrics-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(InterlockedIncrement(&sequence)));
    File out(CreateFileW(staging.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(out.get()==INVALID_HANDLE_VALUE){file.reset();return FileLyricsUpdate::Failed;}
    DWORD written=0;ok=patched.size()<=MAXDWORD&&WriteFile(out.get(),patched.data(),DWORD(patched.size()),&written,nullptr)&&written==patched.size()&&FlushFileBuffers(out.get());
    out.reset();file.reset();
    // ReplaceFile preserves the destination's ACLs and attributes and requires
    // an existing destination; it never silently creates a missing song.
    if(ok)ok=ReplaceFileW(name.c_str(),staging.c_str(),nullptr,0,nullptr,nullptr)!=0;
    if(!ok)DeleteFileW(staging.c_str());
    return ok?FileLyricsUpdate::Updated:FileLyricsUpdate::Failed;
}
}
