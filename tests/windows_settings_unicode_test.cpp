// Runs only beside this test executable, never an installed client.
#include "../native/history_settings.cpp"
#include <cassert>
#include <vector>
#include <thread>
#include <new>
static thread_local bool fail_allocation=false;
void* operator new(std::size_t bytes) {if(fail_allocation){fail_allocation=false;throw std::bad_alloc();}if(auto p=HeapAlloc(GetProcessHeap(),0,bytes?bytes:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {if(p)HeapFree(GetProcessHeap(),0,p);}
void operator delete(void* p,std::size_t) noexcept {if(p)HeapFree(GetProcessHeap(),0,p);}
namespace history {
void StartLogger() {}
void LogActivity(const char*,const std::string&) {}
void QueueDiagnostic(const char*) {}
}
static std::wstring Module() {wchar_t file[32768];assert(GetModuleFileNameW(nullptr,file,32768));return file;}
static std::vector<unsigned char> Bytes(const std::wstring& file) {
 HANDLE h=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);assert(h!=INVALID_HANDLE_VALUE);
 DWORD size=GetFileSize(h,nullptr),read=0;std::vector<unsigned char> data(size);assert(ReadFile(h,data.data(),size,&read,nullptr)&&read==size);CloseHandle(h);return data;
}
static void Spawn(const wchar_t* mode) {
 const auto executable=Module();auto command=L"\""+executable+L"\" "+mode;STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION child{};
 assert(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&start,&child));
 assert(WaitForSingleObject(child.hProcess,15000)==WAIT_OBJECT_0);DWORD code=1;assert(GetExitCodeProcess(child.hProcess,&code));CloseHandle(child.hThread);CloseHandle(child.hProcess);assert(code==0);
}
int main(int argc,char** argv) {
 const auto executable=Module(),directory=executable.substr(0,executable.find_last_of(L'\\'));
 const auto file=directory+L"\\SpotifyHistory.ini";
 const auto destination=directory+L"\\\u65e5\u672c\u8a9e-\U0001f3b5-\u00e9";
 if(argc==2 && std::string(argv[1])=="reload") {
  history::InitSettings(nullptr);const auto s=history::GetSettings();
  assert(s.root==destination && s.save_location==destination);
  assert(!s.downloads&&!s.ogg&&!s.flac&&!s.lyrics&&!s.menu&&!s.metadata);
  wchar_t value[100];GetPrivateProfileStringW(L"Unknown",L"Keep",L"",value,100,file.c_str());assert(std::wstring(value)==L"original");return 0;
 }
 if(argc==2 && std::string(argv[1])=="locked") {
  const auto before=Bytes(file);HANDLE held=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);assert(held!=INVALID_HANDLE_VALUE);
  history::InitSettings(nullptr);assert(history::SetLyrics(false));assert(!history::FlushSettings(5000));
  assert(Bytes(file)==before);CloseHandle(held);return 0;
 }
 if(argc==2 && std::string(argv[1])=="large") {
  const auto legacy=std::string(";")+std::string(600000,'x')+"\r\n[To Disk]\r\nMenu=0\r\nMetadata=0\r\nLyrics=1\r\n";
  HANDLE output=CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);assert(output!=INVALID_HANDLE_VALUE);DWORD count=0;
  assert(WriteFile(output,legacy.data(),DWORD(legacy.size()),&count,nullptr)&&count==legacy.size());CloseHandle(output);
  history::InitSettings(nullptr);assert(history::SetLyrics(false)&&history::FlushSettings(5000));return 0;
 }
 Spawn(L"large");
 DeleteFileW(file.c_str());
 const std::string legacy="; preserve this comment\r\n[To Disk]\r\nDownloads=0\r\nOgg=0\r\nFlac=0\r\nLyrics=0\r\nMenu=0\r\nMetadata=0\r\n[Unknown]\r\nKeep=original\r\n";
 HANDLE h=CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);assert(h!=INVALID_HANDLE_VALUE);DWORD written=0;assert(WriteFile(h,legacy.data(),DWORD(legacy.size()),&written,nullptr)&&written==legacy.size());CloseHandle(h);
 Spawn(L"locked");
 history::InitSettings(nullptr);
 assert(history::SetSaveLocation(destination));assert(history::FlushSettings(5000));
 const auto data=Bytes(file);assert(data.size()>=2&&data[0]==0xff&&data[1]==0xfe);
 const std::wstring text(reinterpret_cast<const wchar_t*>(data.data()+2),(data.size()-2)/2);
 assert(text.find(L"; preserve this comment")!=std::wstring::npos&&text.find(L"[Unknown]")!=std::wstring::npos);
 Spawn(L"reload");
 fail_allocation=true;bool threw=false;
 try {(void)history::GetSettings();}catch(const std::bad_alloc&){threw=true;}
 assert(threw&&TryAcquireSRWLockExclusive(&history::lock));ReleaseSRWLockExclusive(&history::lock);
 const auto before=history::GetSettings();fail_allocation=true;threw=false;
 try {(void)history::SetSaveLocation(destination+L"-next");}catch(const std::bad_alloc&){threw=true;}
 assert(threw&&TryAcquireSRWLockExclusive(&history::lock));ReleaseSRWLockExclusive(&history::lock);
 assert(history::GetSettings().root==before.root&&history::GetSettings().generation==before.generation);
 std::thread a([]{for(unsigned i=0;i<100;i++)assert(history::SetLyrics(i%2));});
 std::thread b([]{for(unsigned i=0;i<100;i++)assert(history::SetOgg(i%2));});a.join();b.join();
 assert(history::SetLyrics(false)&&history::SetOgg(false)&&history::FlushSettings(5000));Spawn(L"reload");
 puts("PASS: Unicode settings BOM, legacy preservation, failed migration, concurrent persistence and fresh-process reload");
}
