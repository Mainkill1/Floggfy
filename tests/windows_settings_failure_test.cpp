// One issue-5 gate. Include the real persistence worker; inject only the INI read
// boundary after a successful read, so the subsequent serialization alloc fails.
#define WIN32_LEAN_AND_MEAN
#include "../native/history_settings.h"
#include <shobjidl.h>
#include <shlobj.h>
#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include "../native/async_log.h"
#include "../native/srw_guard.h"
#include "../native/settings_file.h"
#include <atomic>
#include <cassert>
#include <new>
namespace {
enum class Failure {None,AfterRead,Read};
std::atomic<Failure> failure{Failure::None};
std::atomic<bool> throw_logging{false};
std::atomic<unsigned> logging_failures{0};
std::atomic<unsigned> read_attempts{0},persistence_wakes{0},completed_wakes{0};
thread_local bool fail_allocation=false;
DWORD ObservedWait(HANDLE handle,DWORD timeout) {
    // Only the persistence worker waits indefinitely in the included source.
    // A return to its wait boundary confirms the preceding attempt has finished.
    if(timeout==INFINITE)completed_wakes=persistence_wakes.load();
    const auto result=WaitForSingleObject(handle,timeout);
    if(timeout==INFINITE&&result==WAIT_OBJECT_0)++persistence_wakes;
    return result;
}
}
void* operator new(std::size_t n){if(fail_allocation){fail_allocation=false;throw std::bad_alloc();}if(auto p=HeapAlloc(GetProcessHeap(),0,n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {if(p)HeapFree(GetProcessHeap(),0,p);}
void operator delete(void* p,std::size_t) noexcept {if(p)HeapFree(GetProcessHeap(),0,p);}
namespace history { namespace settings_file {
bool ReadWithFault(const std::wstring& path,std::wstring& text,bool& unicode) {
    ++read_attempts;
    const bool ok=Read(path,text,unicode);
    if(ok&&failure.load()==Failure::AfterRead)fail_allocation=true;
    return ok&&failure.load()!=Failure::Read;
}
}}
#define Read ReadWithFault
#define WaitForSingleObject ObservedWait
#include "../native/history_settings.cpp"
#undef WaitForSingleObject
#undef Read
namespace history {
void StartLogger(){}
void LogActivity(const char*,const std::string&){if(throw_logging.load()){++logging_failures;throw std::bad_alloc();}}
void QueueDiagnostic(const char*){}
}
static std::vector<unsigned char> Bytes(const std::wstring& path) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);assert(file!=INVALID_HANDLE_VALUE);
    const auto size=GetFileSize(file,nullptr);assert(size!=INVALID_FILE_SIZE);std::vector<unsigned char> bytes(size);DWORD count=0;
    assert(ReadFile(file,bytes.data(),DWORD(bytes.size()),&count,nullptr)&&count==bytes.size());CloseHandle(file);return bytes;
}
static LONG Progress(){return InterlockedCompareExchange(&history::persisted_generation,0,0);}
static void Retry(unsigned generation) {
    failure=Failure::None;throw_logging=false;SetEvent(history::settings_signal);
    const auto deadline=GetTickCount64()+5000;
    while(static_cast<LONG>(unsigned(Progress())-generation)<0){assert(GetTickCount64()<deadline);Sleep(1);}
    assert(history::FlushSettings(5000));
}
static void CheckRedundantNotification(const std::wstring& path) {
    const auto before=Bytes(path);
    const auto reads=read_attempts.load(),wake=persistence_wakes.load();
    assert(SetEvent(history::settings_signal));
    const auto deadline=GetTickCount64()+5000;
    // Wait for this notification to be consumed AND its processing to finish.
    // This cannot mistake the preceding successful flush for our notification.
    while(completed_wakes.load()<=wake){assert(GetTickCount64()<deadline);Sleep(1);}
    assert(read_attempts.load()==reads&&Bytes(path)==before);
    assert(history::FlushSettings(5000));
}
int main() {
    wchar_t executable[32768];assert(GetModuleFileNameW(nullptr,executable,32768));
    const auto module=std::wstring(executable),directory=module.substr(0,module.find_last_of(L'\\'));
    const auto path=directory+L"\\SpotifyHistory.ini";DeleteFileW(path.c_str());
    const std::wstring original=L"; preserve unknown configuration comment\r\n[To Disk]\r\nDownloads=0\r\nMetadata=0\r\nMenu=0\r\nLog=0\r\nLyrics=1\r\nOgg=1\r\nFlac=1\r\nUpdateExistingMetadata=0\r\n[Unknown]\r\nKeep=original\r\n";
    assert(history::settings_file::Write(path,original,true));history::InitSettings(nullptr);
    auto before=Bytes(path);const auto persisted=Progress();
    failure=Failure::AfterRead;throw_logging=true;assert(history::SetLyrics(false));
    const auto failed=history::GetSettings().generation;
    assert(!history::FlushSettings(5000)&&Progress()==persisted&&Bytes(path)==before);
    assert(history::persist_failed);
    const auto log_deadline=GetTickCount64()+5000;
    while(!logging_failures.load()){assert(GetTickCount64()<log_deadline);Sleep(1);}
    Retry(failed);
    assert(GetPrivateProfileIntW(L"To Disk",L"Lyrics",9,path.c_str())==0);
    before=Bytes(path);const auto saved=Progress();
    HANDLE held=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);assert(held!=INVALID_HANDLE_VALUE);
    assert(history::SetOgg(false));const auto denied=history::GetSettings().generation;
    assert(!history::FlushSettings(5000)&&Progress()==saved&&Bytes(path)==before);CloseHandle(held);Retry(denied);
    before=Bytes(path);const auto after_write=Progress();failure=Failure::Read;assert(history::SetFlac(false));
    const auto unreadable=history::GetSettings().generation;
    assert(!history::FlushSettings(5000)&&Progress()==after_write&&Bytes(path)==before);Retry(unreadable);
    const auto unicode_path=directory+L"\\\u65e5\u672c\u8a9e-\U0001f3b5";
    assert(history::SetSaveLocation(unicode_path)&&history::SetLyrics(true)&&history::FlushSettings(5000));
    std::wstring text;bool unicode=false;assert(history::settings_file::Read(path,text,unicode)&&unicode);
    assert(text.find(L"; preserve unknown configuration comment")!=std::wstring::npos&&text.find(L"[Unknown]\r\nKeep=original")!=std::wstring::npos);
    assert(GetPrivateProfileIntW(L"To Disk",L"UpdateExistingMetadata",9,path.c_str())==0);
    wchar_t location[32768];GetPrivateProfileStringW(L"To Disk",L"Save Location",L"",location,32768,path.c_str());assert(std::wstring(location)==unicode_path);
    CheckRedundantNotification(path);
    WIN32_FIND_DATAW data{};HANDLE stages=FindFirstFileW((path+L".settings-*").c_str(),&data);assert(stages==INVALID_HANDLE_VALUE);
    assert(DeleteFileW(path.c_str()));
    puts("PASS: failed persistence never acknowledges unwritten generations; retries recover; Unicode/unknown settings survive; redundant notifications perform no file I/O or leave staging files");
}
