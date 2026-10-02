#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../native/file_publication.h"
#include "../native/history_settings.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace history;
static void check(bool ok,const char* label) { if(!ok) { std::fprintf(stderr,"FAIL: %s error=%lu\n",label,GetLastError()); std::exit(1); } }
static std::vector<unsigned char> read(const std::wstring& p) {
    HANDLE h=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    check(h!=INVALID_HANDLE_VALUE,"open test fixture"); std::vector<unsigned char> b(GetFileSize(h,nullptr)); DWORD n;
    check(ReadFile(h,b.data(),DWORD(b.size()),&n,nullptr) && n==b.size(),"read fixture"); CloseHandle(h); return b;
}
int main() {
    wchar_t exe[2048]; GetModuleFileNameW(nullptr,exe,2048); std::wstring dir=exe; dir.resize(dir.find_last_of(L'\\'));
    auto ini=dir+L"\\SpotifyHistory.ini"; DeleteFileW(ini.c_str());
    check(GetFileAttributesW(ini.c_str())==INVALID_FILE_ATTRIBUTES,"INI absent before initialization");
    InitSettings(GetModuleHandleW(nullptr));
    check(GetFileAttributesW(ini.c_str())!=INVALID_FILE_ATTRIBUTES,"missing INI created beside DLL");
    auto defaults=GetSettings();
    check(!defaults.downloads && defaults.menu && defaults.ogg && defaults.flac &&
          defaults.metadata && defaults.log && !defaults.debug_log,
          "generated INI loads complete default switches");
    check(defaults.save_location.empty() && defaults.max_buffered_mib==500,
          "generated INI uses default Music location and 500 MiB buffer");
    check(GetPrivateProfileIntW(L"History",L"StopAfter",UINT(-1),ini.c_str())==UINT(-1),"generated INI omits history limit");
    auto low=dir+L"\\low.ogg",high=dir+L"\\high.ogg",target=dir+L"\\published.ogg";
    Quality qlow,qhigh,qflac; check(ReadQuality(low,qlow) && qlow.bitrate==160000,"real CRC Vorbis low bitrate header");
    check(ReadQuality(high,qhigh) && qhigh.bitrate==320000,"real CRC Vorbis high bitrate header");
    check(ReadQuality(dir+L"\\existing.flac",qflac) && qflac.rate==44100 && qflac.channels==2 && qflac.bits==16,"FLAC STREAMINFO quality");
    check(DecideSave(high,qlow)==SaveDecision::Skip && DecideSave(high,qhigh)==SaveDecision::Skip,"equal and higher file skipped");
    check(DecideSave(dir+L"\\unknown.ogg",qhigh)==SaveDecision::Skip,"unknown existing content preserved");
    auto lo=read(low),hi=read(high); DeleteFileW(target.c_str());
    check(PublishBytes(target,qlow,lo.data(),lo.size())==FilePublication::Saved,"first completed file published");
    check(PublishBytes(target,qlow,hi.data(),hi.size())==FilePublication::Skipped && read(target)==lo,"duplicate skip performs no overwrite");
    check(PublishBytes(target,qhigh,hi.data(),0)==FilePublication::Failed && read(target)==lo,"failed upgrade preserves previous file");
    check(PublishBytes(target,qhigh,hi.data(),hi.size())==FilePublication::Upgraded && read(target)==hi,"verified higher quality replaces lower quality");
    check(PublishBytes(target,qlow,lo.data(),lo.size())==FilePublication::Skipped && read(target)==hi,"downgrade prevented");
    WIN32_FIND_DATAW data; HANDLE files=FindFirstFileW((target+L".complete-*").c_str(),&data);
    check(files==INVALID_HANDLE_VALUE,"no completed staging files remain"); if(files!=INVALID_HANDLE_VALUE) FindClose(files);
    check(SetDownloads(true) && OggEnabled(),"Downloads enabled immediately");
    check(SetOgg(false) && !OggEnabled(),"Ogg disable immediately bypasses capture");
    check(SetOgg(true) && SetDownloads(false) && !OggEnabled(),"Downloads off persists and bypasses capture");
    unsigned epoch=GetSettings().capture_epoch;
    check(SetDownloads(true) && SetDownloads(false) && GetSettings().capture_epoch==epoch+2 && CaptureEpoch()==epoch+2,"missed off-on transitions still advance capture epoch");
    check(FlushSettings(5000),"background settings persisted");
    check(GetPrivateProfileIntW(L"To Disk",L"Downloads",9,ini.c_str())==0,"INI persists disabled setting");
    auto root=dir+L"\\Pending-"+std::to_wstring(GetCurrentProcessId()); epoch=GetSettings().capture_epoch;
    check(SetSaveLocation(root) && GetSettings().root==root,"save directory persistence");
    check(GetFileAttributesW(root.c_str())==INVALID_FILE_ATTRIBUTES && GetSettings().capture_epoch==epoch,"save location does not precreate folders or invalidate capture");
    check(FlushSettings(5000),"background save location persisted");
    DeleteFileW(target.c_str());
    puts("PASS: Windows quality parsing, duplicate skip, atomic upgrade, downgrade protection, failure preservation and settings persistence");
}
