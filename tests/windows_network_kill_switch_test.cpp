#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include "../native/network_kill_switch.h"
#include "../native/settings_file.h"
#include "../native/vendor/minhook/include/MinHook.h"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s (WSA=%d)\n", __LINE__, #x, WSAGetLastError()); return 1; } } while (0)
#define DOWN(x) do { CHECK((x)==SOCKET_ERROR); CHECK(WSAGetLastError()==WSAENETDOWN); } while(0)
extern "C" decltype(&MapViewOfFile) __real___imp_MapViewOfFile;

namespace {
constexpr DWORD child_timeout_ms = 10000;
HANDLE mapping_created = nullptr;
HANDLE resume_helper = nullptr;

// Pause the helper at its first view after CreateFileMappingW has returned.
// The real mapping, settings reader, and Winsock hooks run in both processes.
LPVOID WINAPI PauseBeforeMap(HANDLE mapping,DWORD access,DWORD high,DWORD low,SIZE_T size) {
 if(mapping_created && (!SetEvent(mapping_created) ||
     WaitForSingleObject(resume_helper,child_timeout_ms)!=WAIT_OBJECT_0)) return nullptr;
 return __real___imp_MapViewOfFile(mapping,access,high,low,size);
}
}
extern "C" {
decltype(&MapViewOfFile) __wrap___imp_MapViewOfFile = PauseBeforeMap;
}

int RunChild(const wchar_t* mode,const wchar_t* configuration=L"[Misc]\nNetworkKillSwitch=0\n",bool unreadable=false,int controller_mode=-1) {
 wchar_t path[MAX_PATH]{}; CHECK(GetModuleFileNameW(nullptr,path,MAX_PATH));
 wchar_t ini[MAX_PATH]{}; std::wcscpy(ini,path); wchar_t* slash=std::wcsrchr(ini,L'\\'); CHECK(slash); std::wcscpy(slash+1,L"SpotifyHistory.ini");
 if(configuration) { CHECK(history::settings_file::Write(ini,configuration,GetFileAttributesW(ini)==INVALID_FILE_ATTRIBUTES)); }
 else { CHECK(DeleteFileW(ini)||GetLastError()==ERROR_FILE_NOT_FOUND); }
 history::settings_file::Handle locked(unreadable?
     CreateFileW(ini,GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr):INVALID_HANDLE_VALUE);
 CHECK(!unreadable||locked.value!=INVALID_HANDLE_VALUE);
 wchar_t command[MAX_PATH+100]{};
 SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};
 const bool staged = controller_mode>=0;
 history::settings_file::Handle created(staged ? CreateEventW(&attributes,TRUE,FALSE,nullptr) : INVALID_HANDLE_VALUE);
 history::settings_file::Handle resume(staged ? CreateEventW(&attributes,TRUE,FALSE,nullptr) : INVALID_HANDLE_VALUE);
 if(staged) {
  CHECK(created.value && resume.value);
  swprintf(command,MAX_PATH+100,L"\"%ls\" %ls %llu %llu",path,mode,
      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(created.value)),
      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(resume.value)));
 } else swprintf(command,MAX_PATH+100,L"\"%ls\" %ls",path,mode);
 STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
 CHECK(CreateProcessW(path,command,nullptr,nullptr,staged,0,nullptr,nullptr,&startup,&process));
 history::settings_file::Handle thread(process.hThread), child(process.hProcess);
 if(staged) {
  HANDLE wait_for[]{created.value,child.value};
  CHECK(WaitForMultipleObjects(2,wait_for,FALSE,child_timeout_ms)==WAIT_OBJECT_0);
  CHECK(network::Initialize(GetModuleHandleW(nullptr),true,controller_mode));
  CHECK(network::Enabled()==(controller_mode==1));
  CHECK(network::SetEnabled(controller_mode==0));
  CHECK(network::Enabled()==(controller_mode==0));
  CHECK(SetEvent(resume.value));
 }
 CHECK(WaitForSingleObject(child.value,child_timeout_ms)==WAIT_OBJECT_0);
 DWORD result=1; CHECK(GetExitCodeProcess(child.value,&result));
 if(staged) CHECK(network::Enabled()==(controller_mode==0));
 CHECK(result==0);
 return 0;
}
int WSAAPI ExistingSendHook(SOCKET,const char*,int,int) { return SOCKET_ERROR; }
int main(int argc,char** argv) {
 WSADATA w{}; CHECK(WSAStartup(MAKEWORD(2,2),&w)==0);
 if(argc==4) {
  mapping_created=reinterpret_cast<HANDLE>(static_cast<uintptr_t>(std::strtoull(argv[2],nullptr,10)));
  resume_helper=reinterpret_cast<HANDLE>(static_cast<uintptr_t>(std::strtoull(argv[3],nullptr,10)));
 }
 if(argc>1 && !std::strcmp(argv[1],"--helper-race-enable")) {
  CHECK(RunChild(L"--child",L"[Misc]\nNetworkKillSwitch=0\n",false,0)==0);
  puts("PASS: delayed helper preserves controller enable toggle"); return 0;
 }
 if(argc>1 && !std::strcmp(argv[1],"--helper-race-disable")) {
  CHECK(RunChild(L"--child-off",L"[Misc]\nNetworkKillSwitch=1\n",false,1)==0);
  puts("PASS: delayed helper preserves controller disable toggle"); return 0;
 }
 if(argc>1 && !std::strcmp(argv[1],"--helper-first-config")) {
  // No controller mapping exists yet: a helper must use the controller's exact parser.
  for(const wchar_t* value:{L"",L"2",L"01",L"+1",L" 1",L"1 ",L"1\nNetworkKillSwitch=1",L"0\nNetworkKillSwitch=1"}) {
   const auto configuration=std::wstring(L"[Misc]\nNetworkKillSwitch=")+value+L"\n";
   CHECK(RunChild(L"--child-off",configuration.c_str())==0);
  }
  CHECK(RunChild(L"--child-hidden",L"[Misc]\nNetworkKillSwitch=-1\n")==0);
  CHECK(RunChild(L"--child-off",L"[Misc]\nOther=1\n")==0);
  CHECK(RunChild(L"--child-off",L"[To Disk]\nNetworkKillSwitch=1\n")==0);
  CHECK(RunChild(L"--child-off",nullptr)==0);
  CHECK(RunChild(L"--child-off",L"[Misc]\nNetworkKillSwitch=1\n",true)==0);
  CHECK(RunChild(L"--child-off")==0);
  CHECK(RunChild(L"--child",L"[Misc]\nNetworkKillSwitch=1\n")==0);
  puts("PASS: helper-first strict settings, duplicates, missing/unreadable defaults and valid modes"); return 0;
 }
 if(argc>1 && !std::strcmp(argv[1],"--child-hidden")) {
  CHECK(network::Initialize(GetModuleHandleW(nullptr),false,1));
  CHECK(!network::Available()&&!network::Enabled()&&!network::SetEnabled(true));
  SOCKET s=socket(AF_INET,SOCK_STREAM,0); CHECK(s!=INVALID_SOCKET); closesocket(s); return 0;
 }
 if(argc>1 && !std::strcmp(argv[1],"--hidden")) {
  CHECK(network::Initialize(GetModuleHandleW(nullptr),true,-1)); CHECK(!network::Available());
  CHECK(!network::SetEnabled(true)); SOCKET s=socket(AF_INET,SOCK_STREAM,0); CHECK(s!=INVALID_SOCKET); closesocket(s); return 0;
 }
 if(argc>1 && !std::strcmp(argv[1],"--child-off")) {
  CHECK(network::Initialize(GetModuleHandleW(nullptr),false,0)); CHECK(network::Available()); CHECK(!network::Enabled());
  SOCKET s=socket(AF_INET,SOCK_STREAM,0); CHECK(s!=INVALID_SOCKET); closesocket(s); return 0;
 }
 if(argc>1 && !std::strcmp(argv[1],"--child-failure")) {
  CHECK(MH_Initialize()==MH_OK);
  void* original=nullptr;
  CHECK(MH_CreateHook(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ws2_32.dll"),"send")),reinterpret_cast<void*>(&ExistingSendHook),&original)==MH_OK);
  CHECK(!network::Initialize(GetModuleHandleW(nullptr),false,0));
  CHECK(!network::Available()); CHECK(!network::Enabled()); return 0;
 }
 if(argc>1 && !std::strcmp(argv[1],"--failure")) {
  CHECK(MH_Initialize()==MH_OK);
  void* original=nullptr;
  CHECK(MH_CreateHook(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ws2_32.dll"),"send")),reinterpret_cast<void*>(&ExistingSendHook),&original)==MH_OK);
  CHECK(!network::Initialize(GetModuleHandleW(nullptr),true,1));
  CHECK(!network::Available()); CHECK(!network::Enabled()); CHECK(!network::SetEnabled(true));
  SOCKET s=socket(AF_INET,SOCK_STREAM,0); CHECK(s!=INVALID_SOCKET); closesocket(s);
  CHECK(RunChild(L"--child-off",L"[Misc]\nNetworkKillSwitch=1\n")==0);
  puts("PASS: controller initialization failure leaves helpers off and public state unavailable"); return 0;
 }
 if(argc>1 && !std::strcmp(argv[1],"--child")) {
  CHECK(network::Initialize(GetModuleHandleW(nullptr),false,0)); CHECK(network::Enabled());
  CHECK(socket(AF_INET,SOCK_STREAM,0)==INVALID_SOCKET); CHECK(WSAGetLastError()==WSAENETDOWN); return 0;
 }
 SOCKET listener=socket(AF_INET,SOCK_STREAM,0), client=socket(AF_INET,SOCK_STREAM,0);
 CHECK(listener!=INVALID_SOCKET && client!=INVALID_SOCKET);
 sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 CHECK(bind(listener,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))==0); CHECK(listen(listener,1)==0);
 int size=sizeof(addr); CHECK(getsockname(listener,reinterpret_cast<sockaddr*>(&addr),&size)==0);
 CHECK(connect(client,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))==0);
 SOCKET server=accept(listener,nullptr,nullptr); CHECK(server!=INVALID_SOCKET);
 SOCKET udp=socket(AF_INET,SOCK_DGRAM,0), pending=socket(AF_INET,SOCK_STREAM,0);
 SOCKET six=socket(AF_INET6,SOCK_DGRAM,0); CHECK(udp!=INVALID_SOCKET && pending!=INVALID_SOCKET && six!=INVALID_SOCKET);
 CHECK(network::Initialize(GetModuleHandleW(nullptr),true,0)); CHECK(network::Available()); CHECK(!network::Enabled());
 char data='x',received=0; CHECK(send(client,&data,1,0)==1); CHECK(recv(server,&received,1,0)==1 && received=='x');
 GUID guid=WSAID_CONNECTEX; LPFN_CONNECTEX connect_ex=nullptr; DWORD copied=0;
 CHECK(WSAIoctl(pending,SIO_GET_EXTENSION_FUNCTION_POINTER,&guid,sizeof(guid),&connect_ex,sizeof(connect_ex),&copied,nullptr,nullptr)==0);
 CHECK(connect_ex);
 sockaddr_in local{}; local.sin_family=AF_INET; local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 CHECK(bind(pending,reinterpret_cast<sockaddr*>(&local),sizeof(local))==0);
 CHECK(bind(udp,reinterpret_cast<sockaddr*>(&local),sizeof(local))==0);
 size=sizeof(local); CHECK(getsockname(udp,reinterpret_cast<sockaddr*>(&local),&size)==0);
 GUID send_id=WSAID_WSASENDMSG, recv_id=WSAID_WSARECVMSG;
 LPFN_WSASENDMSG send_msg=nullptr; LPFN_WSARECVMSG recv_msg=nullptr;
 CHECK(WSAIoctl(udp,SIO_GET_EXTENSION_FUNCTION_POINTER,&send_id,sizeof(send_id),&send_msg,sizeof(send_msg),&copied,nullptr,nullptr)==0);
 CHECK(WSAIoctl(udp,SIO_GET_EXTENSION_FUNCTION_POINTER,&recv_id,sizeof(recv_id),&recv_msg,sizeof(recv_msg),&copied,nullptr,nullptr)==0);
 WSABUF datagram_buffer{1,&data}; WSAMSG message{};
 message.name=reinterpret_cast<sockaddr*>(&local); message.namelen=sizeof(local); message.lpBuffers=&datagram_buffer; message.dwBufferCount=1;
 DWORD transferred=0;
 CHECK(send_msg(udp,&message,0,&transferred,nullptr,nullptr)==0 && transferred==1);
 WSAMSG received_message{}; received_message.lpBuffers=&datagram_buffer; received_message.dwBufferCount=1;
 CHECK(recv_msg(udp,&received_message,&transferred,nullptr,nullptr)==0 && transferred==1);
 HMODULE mswsock=LoadLibraryW(L"mswsock.dll"); CHECK(mswsock);
 auto direct_transmit=reinterpret_cast<LPFN_TRANSMITFILE>(reinterpret_cast<void*>(GetProcAddress(mswsock,"TransmitFile")));
 auto direct_accept=reinterpret_cast<LPFN_ACCEPTEX>(reinterpret_cast<void*>(GetProcAddress(mswsock,"AcceptEx")));
 CHECK(direct_transmit && direct_accept);
 CHECK(network::SetEnabled(true)); CHECK(network::Enabled());
 CHECK(!direct_transmit(client,INVALID_HANDLE_VALUE,0,0,nullptr,nullptr,0)); CHECK(WSAGetLastError()==WSAENETDOWN);
 CHECK(socket(AF_INET,SOCK_STREAM,0)==INVALID_SOCKET); CHECK(WSAGetLastError()==WSAENETDOWN);
 CHECK(socket(AF_INET6,SOCK_DGRAM,0)==INVALID_SOCKET); CHECK(WSAGetLastError()==WSAENETDOWN);
 CHECK(WSASocketW(AF_INET,SOCK_DGRAM,0,nullptr,0,0)==INVALID_SOCKET); CHECK(WSAGetLastError()==WSAENETDOWN);
 CHECK(WSASocketA(AF_INET6,SOCK_STREAM,0,nullptr,0,0)==INVALID_SOCKET); CHECK(WSAGetLastError()==WSAENETDOWN);
 CHECK(socket(AF_INET,SOCK_DGRAM,0)==INVALID_SOCKET); CHECK(WSAGetLastError()==WSAENETDOWN);
 CHECK(socket(AF_INET6,SOCK_STREAM,0)==INVALID_SOCKET); CHECK(WSAGetLastError()==WSAENETDOWN);
 DOWN(send(client,&data,1,0)); DOWN(recv(server,&received,1,0));
 DOWN(connect(pending,reinterpret_cast<sockaddr*>(&addr),sizeof(addr)));
 DOWN(sendto(udp,&data,1,0,reinterpret_cast<sockaddr*>(&addr),sizeof(addr)));
 sockaddr_in6 addr6{}; addr6.sin6_family=AF_INET6; addr6.sin6_addr=in6addr_loopback; addr6.sin6_port=addr.sin_port;
 DOWN(sendto(six,&data,1,0,reinterpret_cast<sockaddr*>(&addr6),sizeof(addr6)));
 WSABUF buffer{1,&data}; DWORD count=99,flags=0;
 DOWN(WSASend(client,&buffer,1,&count,0,nullptr,nullptr)); CHECK(count==0);
 count=99; DOWN(WSARecv(server,&buffer,1,&count,&flags,nullptr,nullptr)); CHECK(count==0);
 count=99; DOWN(WSASendTo(udp,&buffer,1,&count,0,reinterpret_cast<sockaddr*>(&local),sizeof(local),nullptr,nullptr)); CHECK(count==0);
 count=99; DOWN(WSARecvFrom(udp,&buffer,1,&count,&flags,nullptr,nullptr,nullptr,nullptr)); CHECK(count==0);
 count=99; DOWN(send_msg(udp,&message,0,&count,nullptr,nullptr)); CHECK(count==0);
 count=99; DOWN(recv_msg(udp,&received_message,&count,nullptr,nullptr)); CHECK(count==0);
 count=99; DOWN(WSASendMsg(udp,&message,0,&count,nullptr,nullptr)); CHECK(count==0);
 DOWN(WSAConnect(pending,reinterpret_cast<sockaddr*>(&addr),sizeof(addr),nullptr,nullptr,nullptr,nullptr));
 DOWN(recvfrom(udp,&data,1,0,nullptr,nullptr));
 CHECK(send(INVALID_SOCKET,&data,1,0)==SOCKET_ERROR); CHECK(WSAGetLastError()==WSAENOTSOCK);
 OVERLAPPED ov{}; ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr); CHECK(ov.hEvent); count=99;
 DOWN(WSASend(client,&buffer,1,nullptr,0,&ov,nullptr));
 CHECK(WaitForSingleObject(ov.hEvent,0)==WAIT_TIMEOUT);
 char accept_buffer[256]{};
 CHECK(!direct_accept(listener,pending,accept_buffer,0,64,64,&count,&ov)); CHECK(WSAGetLastError()==WSAENETDOWN);
 CHECK(!connect_ex(pending,reinterpret_cast<sockaddr*>(&addr),sizeof(addr),nullptr,0,&count,&ov)); CHECK(WSAGetLastError()==WSAENETDOWN); CHECK(count==0);
 // A separately initialized process must observe the controller's enabled flag.
 CHECK(RunChild(L"--child")==0);
 CHECK(RunChild(L"--child-failure")==0); CHECK(network::Enabled());
 CHECK(network::SetEnabled(false)); CHECK(!network::Enabled());
 CHECK(send(client,&data,1,0)==1); CHECK(recv(server,&received,1,0)==1);
 CHECK(send_msg(udp,&message,0,&transferred,nullptr,nullptr)==0 && transferred==1);
 CHECK(recv_msg(udp,&received_message,&transferred,nullptr,nullptr)==0 && transferred==1);
 BOOL connected=connect_ex(pending,reinterpret_cast<sockaddr*>(&addr),sizeof(addr),nullptr,0,&count,&ov);
 if(!connected) { CHECK(WSAGetLastError()==WSA_IO_PENDING); CHECK(WaitForSingleObject(ov.hEvent,3000)==WAIT_OBJECT_0); CHECK(WSAGetOverlappedResult(pending,&ov,&count,FALSE,&flags)); }
 SOCKET accepted=accept(listener,nullptr,nullptr); CHECK(accepted!=INVALID_SOCKET); closesocket(accepted); CloseHandle(ov.hEvent);
 SOCKET recovered=socket(AF_INET,SOCK_DGRAM,0); CHECK(recovered!=INVALID_SOCKET);
 closesocket(recovered); closesocket(six); closesocket(pending); closesocket(udp); closesocket(server); closesocket(client); closesocket(listener);
 puts("PASS: hidden, TCP/UDP IPv4/IPv6, existing sockets, cached extensions, direct exports, overlap rejection, shared helper state, disable recovery");
 WSACleanup(); return 0;
}
