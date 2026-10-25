#define WIN32_LEAN_AND_MEAN
#include "connect_messages_windows.h"
#include "connect_message.h"
#include "history_settings.h"
#include "spotify_hook_discovery.h"
#include "hook_init_state.h"
#include "hook_rollback.h"
#include "vendor/minhook/include/MinHook.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
namespace history {
namespace {
using JsonParser=unsigned char(*)(void*,const void*);
JsonParser decoded_original=nullptr;
std::atomic<bool> decoded_enabled{false};
std::uintptr_t decoded_plain_return=0,decoded_compressed_return=0;
hooks::InitController decoded_init;
hooks::CallbackCounter decoded_callbacks;
HMODULE decoded_module=nullptr; // Retained while the optional detour can exist.
enum class CopyState { Copied,Oversized,Unreadable,InvalidString };
enum class MessageSource { Plain,Decompressed };
struct DecodedRecord {
    std::uint64_t observed_ms=0;
    unsigned length=0;
    unsigned char json_result=0;
    CopyState copy=CopyState::Unreadable;
    MessageSource source=MessageSource::Plain;
    std::array<char,max_connect_message> bytes{};
};
constexpr unsigned decoded_capacity=16;
std::array<DecodedRecord,decoded_capacity> decoded_records;
unsigned decoded_head=0,decoded_tail=0,decoded_count=0;
SRWLOCK decoded_lock=SRWLOCK_INIT;
std::atomic<unsigned long> decoded_omitted{0};
// Only the startup worker touches labels. IDs stay bounded and private; peers
// are numbered within this process, not authenticated identities or device names.
std::array<std::string,32> decoded_senders;
unsigned decoded_sender_count=0;
bool ReadOwnMemory(const void* source,void* target,std::size_t size) noexcept {
    SIZE_T read=0;
    return source&&ReadProcessMemory(GetCurrentProcess(),source,target,size,&read)&&read==size;
}
void CopyDecodedString(const void* native,DecodedRecord& record) noexcept {
    // Audited const-string adapter reads this MSVC x64 layout. Copy metadata,
    // then bytes with OS-checked reads; never invoke/destroy a native C++ object.
    struct NativeString {std::array<unsigned char,16> storage;std::uint64_t size,capacity;};
    static_assert(sizeof(NativeString)==32,"audited MSVC x64 string view");
    NativeString view{};
    if(!ReadOwnMemory(native,&view,sizeof(view)))return;
    if(view.size>view.capacity||(view.capacity<=15&&view.size>15)) {
        record.copy=CopyState::InvalidString;return;
    }
    if(view.size>max_connect_message){record.copy=CopyState::Oversized;return;}
    const void* data=nullptr;
    if(view.capacity<=15)data=view.storage.data();
    else std::memcpy(&data,view.storage.data(),sizeof(data));
    const auto length=std::size_t(view.size);
    if(length) {
        if(view.capacity<=15)std::memcpy(record.bytes.data(),data,length);
        else if(!ReadOwnMemory(data,record.bytes.data(),length))return;
    }
    record.length=unsigned(length);record.copy=CopyState::Copied;
}
void QueueDecodedRecord(const DecodedRecord& record) noexcept {
    if(!TryAcquireSRWLockExclusive(&decoded_lock)) {
        decoded_omitted.fetch_add(1,std::memory_order_relaxed);return;
    }
    if(decoded_count==decoded_capacity)decoded_omitted.fetch_add(1,std::memory_order_relaxed);
    else {
        decoded_records[decoded_tail]=record;
        decoded_tail=(decoded_tail+1)%decoded_capacity;++decoded_count;
    }
    ReleaseSRWLockExclusive(&decoded_lock);
}
bool PopDecodedRecord(DecodedRecord& record) noexcept {
    if(!TryAcquireSRWLockExclusive(&decoded_lock))return false;
    const bool ready=decoded_count!=0;
    if(ready) {
        record=decoded_records[decoded_head];
        SecureZeroMemory(&decoded_records[decoded_head],sizeof(DecodedRecord));
        decoded_head=(decoded_head+1)%decoded_capacity;--decoded_count;
    }
    ReleaseSRWLockExclusive(&decoded_lock);return ready;
}
// Keep the 16 KiB record off the common JSON hook's stack. Unrelated parsing
// takes the small pass-through path, and never copies or queues a message.
__declspec(noinline) unsigned char ObserveDecodedCall(void* output,const void* input,MessageSource source) {
    DecodedRecord record;
    const DWORD previous_error=GetLastError();
    record.observed_ms=GetTickCount64();record.source=source;
    CopyDecodedString(input,record);
    SetLastError(previous_error);
    const unsigned char result=decoded_original(output,input); // Exact original pointer/bytes, once.
    const DWORD original_error=GetLastError();
    record.json_result=result;
    // Invalid JSON has no useful summary and must not retain arbitrary content.
    if(result!=1){SecureZeroMemory(record.bytes.data(),record.bytes.size());record.length=0;}
    QueueDecodedRecord(record);
    SecureZeroMemory(&record,sizeof(record));
    SetLastError(original_error);
    return result;
}
unsigned char ForwardDecodedCall(void* output,const void* input,std::uintptr_t caller) {
    if(!decoded_enabled.load(std::memory_order_acquire)||!LoggingEnabled()||!DebugLoggingEnabled())
        return decoded_original(output,input);
    if(caller==decoded_plain_return)return ObserveDecodedCall(output,input,MessageSource::Plain);
    if(caller==decoded_compressed_return)return ObserveDecodedCall(output,input,MessageSource::Decompressed);
    return decoded_original(output,input);
}
__declspec(noinline) unsigned char DecodedJsonHook(void* output,const void* input) {
    auto callback=decoded_callbacks.Enter();
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    return ForwardDecodedCall(output,input,caller);
}
unsigned SenderLabel(const std::string& sender) {
    if(sender.empty())return 0;
    for(unsigned i=0;i<decoded_sender_count;++i)if(decoded_senders[i]==sender)return i+1;
    if(decoded_sender_count==decoded_senders.size())return 0;
    decoded_senders[decoded_sender_count]=sender;return ++decoded_sender_count;
}
const char* CopyReason(CopyState state) noexcept {
    switch(state) {
        case CopyState::Copied:return "copied";case CopyState::Oversized:return "oversized";
        case CopyState::InvalidString:return "string-layout";default:return "unreadable";
    }
}
std::size_t MappedConnectImageSize(HMODULE module) noexcept {
    if(!module)return 0;
    const auto base=reinterpret_cast<std::uintptr_t>(module);
    auto cursor=base;
    constexpr std::size_t maximum=512u*1024*1024;
    for(unsigned region=0;region<128;++region) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<const void*>(cursor),&info,sizeof(info)))return 0;
        if(info.AllocationBase!=module)break;
        if(info.Type!=MEM_IMAGE||!info.RegionSize||info.RegionSize>maximum-(cursor-base))return 0;
        cursor+=info.RegionSize;
        if(cursor<base)return 0;
    }
    return cursor>base?cursor-base:0;
}
}
void StartDecodedConnectMessages(HMODULE spotify_module,bool requested) noexcept {
    if(!requested||!LoggingEnabled()||!DebugLoggingEnabled())return;
    const auto now=static_cast<std::uint64_t>(GetTickCount64());
    if(!decoded_init.TryBegin(now))return;
    spotify::ConnectDecodeTargets found{};
    const auto size=MappedConnectImageSize(spotify_module);
    const auto result=spotify::DiscoverConnectDecodeTargets(reinterpret_cast<const std::uint8_t*>(spotify_module),size,found);
    if(result!=spotify::DiscoveryResult::Found) {
        HistoryLog("decoded Connect observer unavailable: message layout not verified; commands unchanged");
        decoded_init.MarkUnsupported();return;
    }
    const auto initialized=MH_Initialize();
    if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED) {
        decoded_init.Retry(now);return;
    }
    const auto base=reinterpret_cast<std::uintptr_t>(spotify_module);
    void* target=reinterpret_cast<void*>(base+found.parser);
    HMODULE retained=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(target),&retained)) {
        decoded_init.Retry(now);return;
    }
    if(MH_CreateHook(target,reinterpret_cast<void*>(DecodedJsonHook),reinterpret_cast<void**>(&decoded_original))!=MH_OK) {
        FreeLibrary(retained);decoded_init.Retry(now);return;
    }
    decoded_module=retained;
    decoded_plain_return=base+found.plain_return;decoded_compressed_return=base+found.decoded_return;
    decoded_enabled.store(true,std::memory_order_release);
    if(MH_EnableHook(target)!=MH_OK) {
        decoded_enabled.store(false,std::memory_order_release);
        MH_DisableHook(target);
        // Keep our one created trampoline and module reference. Even an unusual
        // partial activation cannot race a freed trampoline; do not retry/stack.
        decoded_init.MarkUnsupported();
        HistoryLog("decoded Connect observer unavailable: activation incomplete; observation disabled");return;
    }
    decoded_init.Activate();
    HistoryLog("decoded Connect observer active: complete messages only; read-only; pause/resume receipt is not execution");
}
void DrainDecodedConnectMessages() noexcept {
    for(unsigned i=0;i<decoded_capacity;++i) {
        DecodedRecord record;
        if(!PopDecodedRecord(record))break;
        try {
            if(LoggingEnabled()&&DebugLoggingEnabled()) {
                char line[350];const auto* source=record.source==MessageSource::Plain?"plain":"decompressed";
                ConnectMessage message;
                if(record.json_result==1&&record.copy==CopyState::Copied&&
                   SummarizeConnectMessage({record.bytes.data(),record.length},message)) {
                    const auto label=SenderLabel(message.sender);
                    char sender[32],id[24];
                    if(label)std::snprintf(sender,sizeof(sender),"peer-%u",label);else std::snprintf(sender,sizeof(sender),"unknown");
                    if(message.has_message_id)std::snprintf(id,sizeof(id),"%u",message.message_id);else std::snprintf(id,sizeof(id),"unknown");
                    std::snprintf(line,sizeof(line),
                        "Connect decoded request action=%s message_id=%s sender=%s mono=%.3f source=%s json_ok=1 execution=unobserved",
                        ConnectActionName(message.action),id,sender,double(record.observed_ms)/1000.0,source);
                } else std::snprintf(line,sizeof(line),
                    "Connect decoded observation mono=%.3f source=%s json_result=%u copy=%s summary=unavailable; commands unchanged",
                    double(record.observed_ms)/1000.0,source,unsigned(record.json_result),CopyReason(record.copy));
                HistoryLog(line);
            }
        }catch(...) {/* An enrichment allocation must not terminate the startup worker. */}
        SecureZeroMemory(&record,sizeof(record));
    }
    const auto omitted=decoded_omitted.exchange(0,std::memory_order_relaxed);
    if(omitted&&LoggingEnabled()&&DebugLoggingEnabled()) {
        char line[120];std::snprintf(line,sizeof(line),"Connect decoded observations omitted=%lu; commands unchanged",omitted);HistoryLog(line);
    }
}
}
