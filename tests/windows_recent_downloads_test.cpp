#include "../native/recent_downloads.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {
std::wstring Song(unsigned i) {return L"C:\\Music\\song-"+std::to_wstring(i)+L".flac";}
std::vector<history::RecentDownload> Snapshot() {
    std::vector<history::RecentDownload> rows;
    const ULONGLONG deadline=GetTickCount64()+10000;
    while(GetTickCount64()<deadline) {
        if(history::ReadRecentDownloads(rows))return rows;
        Sleep(0);
    }
    assert(!"Recent-download snapshot timed out");return {};
}
void Check(const std::vector<history::RecentDownload>& rows) {
    assert(rows.size()<=history::kRecentDownloadLimit);
    for(size_t i=0;i<rows.size();++i) {
        assert(!rows[i].title.empty()&&!rows[i].path.empty());
        for(auto c:rows[i].title)assert(c>=32&&!(c>=127&&c<=159));
        for(size_t j=0;j<i;++j)assert(CompareStringOrdinal(rows[i].path.c_str(),-1,rows[j].path.c_str(),-1,TRUE)!=CSTR_EQUAL);
    }
}
void FreshProcess() {
    wchar_t executable[32768];assert(GetModuleFileNameW(nullptr,executable,32768));
    std::wstring command=L"\""+std::wstring(executable)+L"\" fresh";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    assert(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process));
    assert(WaitForSingleObject(process.hProcess,30000)==WAIT_OBJECT_0);
    DWORD code=1;assert(GetExitCodeProcess(process.hProcess,&code));CloseHandle(process.hThread);CloseHandle(process.hProcess);assert(code==0);
}
DWORD WINAPI Writer(void* context) {
    const unsigned lane=static_cast<unsigned>(reinterpret_cast<ULONG_PTR>(context));
    for(unsigned i=0;i<500;++i) {
        const unsigned id=lane*1000+i;
        history::RecordRecentDownload(L"track "+std::to_wstring(id),Song(id),id);
    }
    return 0;
}
}
int main(int argc,char**) {
    assert(Snapshot().empty());
    if(argc==2)return 0;
    history::RecordRecentDownload(L"relative",L"relative.flac",1);
    history::RecordRecentDownload(L"control",L"C:\\invalid\n.flac",2);
    history::RecordRecentDownload(L"empty",L"",3);
    assert(Snapshot().empty());
    for(unsigned i=0;i<12;++i)history::RecordRecentDownload(L"song "+std::to_wstring(i),Song(i),i+1);
    auto rows=Snapshot();assert(rows.size()==10);
    for(unsigned i=0;i<10;++i)assert(rows[i].path==Song(11-i)&&rows[i].bytes==12-i);
    const auto stable=rows;
    history::RecordRecentDownload(L"\u65e5\u672c\u8a9e \U0001f3b5\n\t title",L"c:/MUSIC/./song-5.flac",0x123456789ULL);
    rows=Snapshot();assert(rows.size()==10);
    assert(rows[0].title==L"\u65e5\u672c\u8a9e \U0001f3b5   title");
    assert(rows[0].bytes==0x123456789ULL&&rows[0].path==L"c:\\MUSIC\\song-5.flac");
    const unsigned order[]={11,10,9,8,7,6,4,3,2};
    for(size_t i=0;i<9;++i)assert(rows[i+1].path==Song(order[i]));
    // A visible menu owns a stable copy even after the global list changes.
    assert(stable[0].path==Song(11)&&stable[0].title==L"song 11");
    history::RecordRecentDownload(L"",L"\\\\server\\share\\\u66f2.ogg",99);
    rows=Snapshot();assert(rows[0].title==L"\u66f2.ogg"&&rows[0].bytes==99);
    Check(rows);
    history::RecordRecentDownload(std::wstring(1023,L'x')+L"\U0001f3b5",Song(9000),9000);
    assert(Snapshot()[0].title==std::wstring(1023,L'x'));
    FreshProcess();
    HANDLE writers[4];
    for(ULONG_PTR i=0;i<4;++i){writers[i]=CreateThread(nullptr,0,Writer,reinterpret_cast<void*>(i),0,nullptr);assert(writers[i]);}
    DWORD wait=WAIT_TIMEOUT;
    do {
        const auto before=rows;
        if(history::ReadRecentDownloads(rows))Check(rows);
        else {
            assert(rows.size()==before.size());
            for(size_t i=0;i<rows.size();++i)assert(rows[i].path==before[i].path&&rows[i].bytes==before[i].bytes);
        }
        wait=WaitForMultipleObjects(4,writers,TRUE,0);
        assert(wait==WAIT_TIMEOUT||wait==WAIT_OBJECT_0);
    }while(wait==WAIT_TIMEOUT);
    for(auto h:writers)CloseHandle(h);
    for(unsigned i=0;i<10;++i)history::RecordRecentDownload(L"final "+std::to_wstring(i),Song(10000+i),i);
    rows=Snapshot();assert(rows.size()==10);Check(rows);
    for(unsigned i=0;i<10;++i)assert(rows[i].path==Song(10009-i)&&rows[i].bytes==9-i);
    FreshProcess();
    puts("PASS: session recent downloads max10, Windows path dedup, Unicode, stable snapshots, concurrency and fresh-process empty state");
}
