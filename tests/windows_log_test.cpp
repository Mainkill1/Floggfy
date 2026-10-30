#include "../native/async_log.h"
#include "../native/history_settings.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace history;
int main(){
 wchar_t root[2048];GetTempPathW(2048,root);std::wstring path=std::wstring(root)+L"floggfy-log-test-"+std::to_wstring(GetCurrentProcessId())+L".txt";
 HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);assert(h!=INVALID_HANDLE_VALUE);
 const size_t cap=5*1024*1024;
 // Fill exactly to the cap with complete lines, including a UTF-8 boundary.
 const size_t retained=4*1024*1024;
 const std::string utf8_line="α "+std::string(1019,'x')+"\r\n";
 assert(utf8_line.size()==1024);
 std::string data;for(size_t i=0;i<cap/utf8_line.size();++i)data+=utf8_line;
 DWORD written;assert(WriteFile(h,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size());CloseHandle(h);
 assert(WriteCappedLog(path,"new\r\n"));
 h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);LARGE_INTEGER size;assert(GetFileSizeEx(h,&size));
 std::string actual(size_t(size.QuadPart),'\0');DWORD got;assert(ReadFile(h,actual.data(),DWORD(actual.size()),&got,nullptr)&&got==actual.size());CloseHandle(h);
 assert(actual==data.substr(cap-retained)+"new\r\n");
 // Compaction must leave headroom rather than rewrite almost 5 MiB per line.
 assert(WriteCappedLog(path,"again\r\n"));
 h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);assert(GetFileSizeEx(h,&size));
 actual.assign(size_t(size.QuadPart),'\0');assert(ReadFile(h,actual.data(),DWORD(actual.size()),&got,nullptr));CloseHandle(h);
 assert(actual==data.substr(cap-retained)+"new\r\nagain\r\n");
 assert(!WriteCappedLog(path,std::string(5*1024*1024+1,'x')));
 assert(!WriteCappedLog(path+L"\\missing","failed test\r\n"));
 DeleteFileW(path.c_str());puts("PASS: actual Windows log oldest-line eviction, oversized record and unwritable destination");
 return 0;
}
