#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include "network_kill_switch.h"
#include "network_kill_switch_settings.h"
#include "../vendor/minhook/include/MinHook.h"
#include <atomic>
#include <cstdint>
#include <cwchar>
#include <cstring>

namespace network {
namespace {
// This is a user-mode Winsock boundary, not a firewall. Previously submitted
// overlapped I/O, raw native NT I/O, and cached RIO tables are outside coverage.
// The mapping and trampolines live until process exit: helpers may outlive main.
HANDLE state_mapping = nullptr;
// A new page-file mapping starts zeroed. Both published states must be
// distinct from unset so a late helper cannot overwrite a controller toggle.
constexpr LONG state_unset = 0;
constexpr LONG state_off = 1;
constexpr LONG state_on = 2;
volatile LONG* shared_state = nullptr;
static_assert(__atomic_always_lock_free(sizeof(LONG),nullptr));
std::atomic<bool> available{false};
std::atomic<bool> callbacks_active{false};
bool controller = false;

bool Requested() {
    return callbacks_active.load(std::memory_order_acquire) && shared_state &&
        __atomic_load_n(shared_state,__ATOMIC_ACQUIRE) == state_on;
}
bool IsInternetFamily(int family) { return family == AF_INET || family == AF_INET6; }
bool BlockSocket(SOCKET socket) {
    if (!Requested()) return false;
    const int previous_error = WSAGetLastError();
    WSAPROTOCOL_INFOW info{};
    int size = sizeof(info);
    const bool block = getsockopt(socket, SOL_SOCKET, SO_PROTOCOL_INFOW,
        reinterpret_cast<char*>(&info), &size) == 0 && IsInternetFamily(info.iAddressFamily);
    WSASetLastError(previous_error);
    return block;
}
int Down(LPDWORD transferred = nullptr) {
    if (transferred) *transferred = 0;
    WSASetLastError(WSAENETDOWN);
    return SOCKET_ERROR;
}

// Ordinary operations deliberately retain the native API signature and return
// convention. A synchronous rejection never queues an overlapped completion.
#define SOCKET_HOOK(name, signature, arguments, output) \
    decltype(&::name) original_##name = nullptr; \
    int WSAAPI Hook_##name signature { \
        if (BlockSocket(s)) return Down(output); \
        return original_##name arguments; \
    }
SOCKET_HOOK(connect, (SOCKET s,const sockaddr* a,int n), (s,a,n), nullptr)
SOCKET_HOOK(WSAConnect, (SOCKET s,const sockaddr* a,int n,LPWSABUF c,LPWSABUF d,LPQOS q,LPQOS g), (s,a,n,c,d,q,g), nullptr)
SOCKET_HOOK(send, (SOCKET s,const char* b,int n,int f), (s,b,n,f), nullptr)
SOCKET_HOOK(sendto, (SOCKET s,const char* b,int n,int f,const sockaddr* a,int l), (s,b,n,f,a,l), nullptr)
SOCKET_HOOK(recv, (SOCKET s,char* b,int n,int f), (s,b,n,f), nullptr)
SOCKET_HOOK(recvfrom, (SOCKET s,char* b,int n,int f,sockaddr* a,int* l), (s,b,n,f,a,l), nullptr)
SOCKET_HOOK(WSASend, (SOCKET s,LPWSABUF b,DWORD n,LPDWORD c,DWORD f,LPWSAOVERLAPPED o,LPWSAOVERLAPPED_COMPLETION_ROUTINE r), (s,b,n,c,f,o,r), c)
SOCKET_HOOK(WSARecv, (SOCKET s,LPWSABUF b,DWORD n,LPDWORD c,LPDWORD f,LPWSAOVERLAPPED o,LPWSAOVERLAPPED_COMPLETION_ROUTINE r), (s,b,n,c,f,o,r), c)
SOCKET_HOOK(WSASendTo, (SOCKET s,LPWSABUF b,DWORD n,LPDWORD c,DWORD f,const sockaddr* a,int l,LPWSAOVERLAPPED o,LPWSAOVERLAPPED_COMPLETION_ROUTINE r), (s,b,n,c,f,a,l,o,r), c)
SOCKET_HOOK(WSARecvFrom, (SOCKET s,LPWSABUF b,DWORD n,LPDWORD c,LPDWORD f,sockaddr* a,LPINT l,LPWSAOVERLAPPED o,LPWSAOVERLAPPED_COMPLETION_ROUTINE r), (s,b,n,c,f,a,l,o,r), c)
SOCKET_HOOK(bind, (SOCKET s,const sockaddr* a,int n), (s,a,n), nullptr)
SOCKET_HOOK(listen, (SOCKET s,int n), (s,n), nullptr)
#undef SOCKET_HOOK

decltype(&::socket) original_socket = nullptr;
SOCKET WSAAPI Hook_socket(int family,int type,int protocol) {
    if (Requested() && IsInternetFamily(family)) { Down(); return INVALID_SOCKET; }
    return original_socket(family,type,protocol);
}
decltype(&::WSASocketA) original_WSASocketA = nullptr;
decltype(&::WSASocketW) original_WSASocketW = nullptr;
SOCKET WSAAPI Hook_WSASocketA(int family,int type,int protocol,LPWSAPROTOCOL_INFOA info,GROUP group,DWORD flags) {
    if (Requested() && IsInternetFamily(family == FROM_PROTOCOL_INFO && info ? info->iAddressFamily : family)) { Down(); return INVALID_SOCKET; }
    return original_WSASocketA(family,type,protocol,info,group,flags);
}
SOCKET WSAAPI Hook_WSASocketW(int family,int type,int protocol,LPWSAPROTOCOL_INFOW info,GROUP group,DWORD flags) {
    if (Requested() && IsInternetFamily(family == FROM_PROTOCOL_INFO && info ? info->iAddressFamily : family)) { Down(); return INVALID_SOCKET; }
    return original_WSASocketW(family,type,protocol,info,group,flags);
}
decltype(&::accept) original_accept = nullptr;
SOCKET WSAAPI Hook_accept(SOCKET s,sockaddr* a,int* n) {
    if (BlockSocket(s)) { Down(); return INVALID_SOCKET; }
    return original_accept(s,a,n);
}
decltype(&::WSAAccept) original_WSAAccept = nullptr;
SOCKET WSAAPI Hook_WSAAccept(SOCKET s,sockaddr* a,LPINT n,LPCONDITIONPROC condition,DWORD_PTR context) {
    if (BlockSocket(s)) { Down(); return INVALID_SOCKET; }
    return original_WSAAccept(s,a,n,condition,context);
}

decltype(&::WSAIoctl) original_WSAIoctl = nullptr;
constexpr GUID connect_id = WSAID_CONNECTEX;
constexpr GUID accept_id = WSAID_ACCEPTEX;
constexpr GUID sendmsg_id = WSAID_WSASENDMSG;
constexpr GUID recvmsg_id = WSAID_WSARECVMSG;
constexpr GUID transmitfile_id = WSAID_TRANSMITFILE;
constexpr GUID transmitpackets_id = WSAID_TRANSMITPACKETS;
void* UnhookedExtension(void* pointer);
// Wrappers resolve the socket's provider, not a global first-provider pointer.
// Returned pointers remain wrappers if the menu is toggled after discovery.
template<typename T> T Extension(SOCKET s,const GUID& id) {
    T result = nullptr;
    DWORD bytes = 0;
    if (original_WSAIoctl(s,SIO_GET_EXTENSION_FUNCTION_POINTER,
        const_cast<GUID*>(&id),sizeof(id),&result,sizeof(result),&bytes,nullptr,nullptr) == SOCKET_ERROR) return nullptr;
    return reinterpret_cast<T>(UnhookedExtension(reinterpret_cast<void*>(result)));
}
BOOL PASCAL Hook_ConnectEx(SOCKET s,const sockaddr* a,int n,PVOID data,DWORD length,LPDWORD bytes,LPOVERLAPPED overlapped) {
    if (BlockSocket(s)) { Down(bytes); return FALSE; }
    auto original = Extension<LPFN_CONNECTEX>(s,connect_id);
    return original ? original(s,a,n,data,length,bytes,overlapped) : FALSE;
}
BOOL PASCAL Hook_AcceptEx(SOCKET s,SOCKET incoming,PVOID data,DWORD length,DWORD local,DWORD remote,LPDWORD bytes,LPOVERLAPPED overlapped) {
    if (BlockSocket(s)) { Down(bytes); return FALSE; }
    auto original = Extension<LPFN_ACCEPTEX>(s,accept_id);
    return original ? original(s,incoming,data,length,local,remote,bytes,overlapped) : FALSE;
}
int PASCAL Hook_SendMsg(SOCKET s,LPWSAMSG message,DWORD flags,LPDWORD bytes,LPWSAOVERLAPPED overlapped,LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) {
    if (BlockSocket(s)) return Down(bytes);
    auto original = Extension<LPFN_WSASENDMSG>(s,sendmsg_id);
    return original ? original(s,message,flags,bytes,overlapped,completion) : SOCKET_ERROR;
}
int PASCAL Hook_RecvMsg(SOCKET s,LPWSAMSG message,LPDWORD bytes,LPWSAOVERLAPPED overlapped,LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) {
    if (BlockSocket(s)) return Down(bytes);
    auto original = Extension<LPFN_WSARECVMSG>(s,recvmsg_id);
    return original ? original(s,message,bytes,overlapped,completion) : SOCKET_ERROR;
}
BOOL PASCAL Hook_TransmitFile(SOCKET s,HANDLE file,DWORD length,DWORD packet,LPOVERLAPPED overlapped,LPTRANSMIT_FILE_BUFFERS buffers,DWORD flags) {
    if (BlockSocket(s)) { Down(); return FALSE; }
    auto original = Extension<LPFN_TRANSMITFILE>(s,transmitfile_id);
    return original ? original(s,file,length,packet,overlapped,buffers,flags) : FALSE;
}
BOOL PASCAL Hook_TransmitPackets(SOCKET s,LPTRANSMIT_PACKETS_ELEMENT packets,DWORD count,DWORD size,LPOVERLAPPED overlapped,DWORD flags) {
    if (BlockSocket(s)) { Down(); return FALSE; }
    auto original = Extension<LPFN_TRANSMITPACKETS>(s,transmitpackets_id);
    return original ? original(s,packets,count,size,overlapped,flags) : FALSE;
}
int WSAAPI Hook_WSAIoctl(SOCKET s,DWORD code,LPVOID input,DWORD input_size,LPVOID output,DWORD output_size,LPDWORD returned,LPWSAOVERLAPPED overlapped,LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) {
    // Reject new extension/RIO discovery and potentially network-producing
    // provider controls while enabled. Existing ordinary extension wrappers
    // check state on each invocation; old RIO/native handles are not covered.
    if (BlockSocket(s)) return Down(returned);
    int result = original_WSAIoctl(s,code,input,input_size,output,output_size,returned,overlapped,completion);
    if (result != 0 || code != SIO_GET_EXTENSION_FUNCTION_POINTER || !input ||
        input_size != sizeof(GUID) || !output || output_size < sizeof(void*) ||
        !returned || *returned != sizeof(void*)) return result;
    const auto& id = *static_cast<const GUID*>(input);
    void* replacement = nullptr;
#define REPLACE_EXTENSION(guid, function) if (!std::memcmp(&id,&guid,sizeof(GUID))) replacement = reinterpret_cast<void*>(&function)
    REPLACE_EXTENSION(connect_id,Hook_ConnectEx);
    REPLACE_EXTENSION(accept_id,Hook_AcceptEx);
    REPLACE_EXTENSION(sendmsg_id,Hook_SendMsg);
    REPLACE_EXTENSION(recvmsg_id,Hook_RecvMsg);
    REPLACE_EXTENSION(transmitfile_id,Hook_TransmitFile);
    REPLACE_EXTENSION(transmitpackets_id,Hook_TransmitPackets);
#undef REPLACE_EXTENSION
    if (replacement) std::memcpy(output,&replacement,sizeof(replacement));
    return result;
}

// WSASendMsg also has a public export. Its provider extension pointer is a
// distinct implementation, so retain a direct trampoline for this entry point.
decltype(&::WSASendMsg) original_WSASendMsg = nullptr;
int WSAAPI Hook_WSASendMsg(SOCKET s,LPWSAMSG message,DWORD flags,LPDWORD bytes,LPWSAOVERLAPPED overlapped,LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) {
    if (BlockSocket(s)) return Down(bytes);
    return original_WSASendMsg(s,message,flags,bytes,overlapped,completion);
}

LPFN_ACCEPTEX original_direct_AcceptEx = nullptr;
LPFN_TRANSMITFILE original_direct_TransmitFile = nullptr;
BOOL PASCAL Hook_DirectAcceptEx(SOCKET s,SOCKET incoming,PVOID data,DWORD length,DWORD local,DWORD remote,LPDWORD bytes,LPOVERLAPPED overlapped) {
    if (BlockSocket(s)) { Down(bytes); return FALSE; }
    return original_direct_AcceptEx(s,incoming,data,length,local,remote,bytes,overlapped);
}
BOOL PASCAL Hook_DirectTransmitFile(SOCKET s,HANDLE file,DWORD length,DWORD packet,LPOVERLAPPED overlapped,LPTRANSMIT_FILE_BUFFERS buffers,DWORD flags) {
    if (BlockSocket(s)) { Down(); return FALSE; }
    return original_direct_TransmitFile(s,file,length,packet,overlapped,buffers,flags);
}

bool OpenState(HMODULE module,int& mode) {
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(module,path,32768);
    if (!length || length >= 32768) return false;
    uint64_t hash = 14695981039346656037ULL;
    for (DWORD index=0; index<length; ++index) {
        wchar_t character = path[index];
        if (character >= L'A' && character <= L'Z') character += L'a'-L'A';
        hash = (hash ^ static_cast<uint16_t>(character)) * 1099511628211ULL;
    }
    if (!controller) {
        wchar_t* slash = std::wcsrchr(path,L'\\');
        if (!slash || static_cast<size_t>(slash-path)+20 >= 32768) return false;
        std::wcscpy(slash+1,L"SpotifyHistory.ini");
        ReadConfiguredMode(path,mode);
    }
    if (mode < 0) return true;
    wchar_t name[100]{};
    swprintf(name,100,L"Local\\FloggfyNetworkKillSwitch-%016llx",static_cast<unsigned long long>(hash));
    state_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(LONG),name);
    if (!state_mapping) return false;
    shared_state = static_cast<volatile LONG*>(MapViewOfFile(state_mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(LONG)));
    if (!shared_state) { CloseHandle(state_mapping); state_mapping=nullptr; return false; }
    const LONG configured_state = mode == 1 ? state_on : state_off;
    if (controller) InterlockedExchange(shared_state,configured_state);
    else InterlockedCompareExchange(shared_state,configured_state,state_unset);
    return true;
}
struct HookEntry { const char* name; void* replacement; void** original; void* target; bool mswsock = false; };
#define ENTRY(name) {#name,reinterpret_cast<void*>(&Hook_##name),reinterpret_cast<void**>(&original_##name),nullptr}
HookEntry hooks[] = {
    ENTRY(socket), ENTRY(WSASocketA), ENTRY(WSASocketW), ENTRY(connect), ENTRY(WSAConnect),
    ENTRY(send), ENTRY(sendto), ENTRY(recv), ENTRY(recvfrom), ENTRY(WSASend), ENTRY(WSASendTo),
    ENTRY(WSARecv), ENTRY(WSARecvFrom), ENTRY(WSAIoctl), ENTRY(WSASendMsg),
    ENTRY(bind), ENTRY(listen), ENTRY(accept), ENTRY(WSAAccept),
    {"AcceptEx",reinterpret_cast<void*>(&Hook_DirectAcceptEx),reinterpret_cast<void**>(&original_direct_AcceptEx),nullptr,true},
    {"TransmitFile",reinterpret_cast<void*>(&Hook_DirectTransmitFile),reinterpret_cast<void**>(&original_direct_TransmitFile),nullptr,true}
};
#undef ENTRY
void* UnhookedExtension(void* pointer) {
    for (const auto& hook : hooks)
        if (pointer == hook.target && *hook.original) return *hook.original;
    return pointer;
}
}

bool Enabled() {
    return Available() && Requested();
}
bool Available() { return available.load(std::memory_order_acquire); }
bool SetEnabled(bool enabled) {
    if (!controller || !Available()) return false;
    InterlockedExchange(shared_state,enabled ? state_on : state_off);
    return true;
}
bool Initialize(HMODULE proxy,bool controlling,int initial_mode) {
    controller = controlling;
    if (!OpenState(proxy,initial_mode)) return false;
    if (initial_mode < 0) return true;
    const auto initialization_failed = [] {
        // A failed controller has no working menu toggle. Do not leave helpers
        // enforcing an enabled flag that the controller can no longer change.
        // A helper failure must never turn off protection in healthy processes.
        callbacks_active.store(false,std::memory_order_release);
        if (controller && shared_state) InterlockedExchange(shared_state,state_off);
        return false;
    };
    HMODULE winsock = LoadLibraryW(L"ws2_32.dll");
    HMODULE mswsock = LoadLibraryW(L"mswsock.dll");
    if (!winsock || !mswsock) return initialization_failed();
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return initialization_failed();
    bool prepared = true;
    for (auto& hook : hooks) {
        hook.target = reinterpret_cast<void*>(GetProcAddress(hook.mswsock ? mswsock : winsock,hook.name));
        if (!hook.target || MH_CreateHook(hook.target,hook.replacement,hook.original) != MH_OK) { prepared=false; break; }
    }
    if (prepared) for (const auto& hook : hooks) {
        if (MH_QueueEnableHook(hook.target) != MH_OK) { prepared=false; break; }
    }
    // Callbacks must enforce the requested state during the apply operation,
    // before public Available can report that every hook was installed.
    if (prepared) callbacks_active.store(true,std::memory_order_release);
    if (prepared && MH_ApplyQueued() == MH_OK) {
        available.store(true,std::memory_order_release);
        return true;
    }
    initialization_failed();
    // Keep trampolines allocated if a callback could have entered during a
    // partial apply. Do not uninitialize MinHook: the app owns other hooks.
    for (const auto& hook : hooks) if (hook.target && *hook.original) {
        MH_QueueDisableHook(hook.target);
        MH_DisableHook(hook.target);
    }
    return false;
}
}
