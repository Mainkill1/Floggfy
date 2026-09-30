#include "../native/async_log.h"
#include "../native/history_settings.h"
#include <cassert>
#include <cstdio>
using namespace history;
int main(){
 wchar_t root[2048];GetTempPathW(2048,root);std::wstring path=std::wstring(root)+L"floggfy-log-test-"+std::to_wstring(GetCurrentProcessId())+L".txt";
 HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);assert(h!=INVALID_HANDLE_VALUE);
 LARGE_INTEGER pos;pos.QuadPart=5*1024*1024-4;assert(SetFilePointerEx(h,pos,nullptr,FILE_BEGIN)&&SetEndOfFile(h));CloseHandle(h);
 assert(WriteCappedLog(path,"finished test\r\n"));
 h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);LARGE_INTEGER size;assert(GetFileSizeEx(h,&size)&&size.QuadPart==15);CloseHandle(h);
 assert(!WriteCappedLog(path,std::string(5*1024*1024+1,'x')));
 assert(!WriteCappedLog(path+L"\\missing","failed test\r\n"));
 DeleteFileW(path.c_str());puts("PASS: actual Windows log cap/reset, oversized record and unwritable destination");
}
