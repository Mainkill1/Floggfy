// One executable exercises discovery, read-only forwarding and isolated settings.
#include "../native/history_settings.cpp"
#include "../native/connect_messages_windows.cpp"
#include <cassert>
#include <vector>
void TestConnectMessages();
void TestConnectHookDiscovery();
void TestConnectHookDiscovery(const std::uint8_t*,std::size_t);
namespace {
std::vector<std::string> decoded_lines;
unsigned native_calls=0;
unsigned char native_result=1;
const void* expected_input=nullptr;
void* expected_output=nullptr;
unsigned char NativeParse(void* output,const void* input) {
    ++native_calls;assert(input==expected_input&&output==expected_output);
    *static_cast<unsigned*>(output)=0xabcdef;
    SetLastError(0x10203040);return native_result;
}
struct TestString {
    std::array<unsigned char,16> storage{};
    std::uint64_t size=0,capacity=15;
    explicit TestString(const std::string& text) {
        size=text.size();
        if(size<=15)std::memcpy(storage.data(),text.data(),text.size());
        else {capacity=text.size();const auto* pointer=text.data();std::memcpy(storage.data(),&pointer,sizeof(pointer));}
    }
};
bool Has(const char* text) {
    for(const auto& line:decoded_lines)if(line.find(text)!=std::string::npos)return true;
    return false;
}
void DecodedSettings() {
    assert(!history::Settings{}.debug_networking);
    wchar_t executable[32768];assert(GetModuleFileNameW(nullptr,executable,32768));
    const std::wstring exe=executable,ini=exe.substr(0,exe.find_last_of(L'\\'))+L"\\SpotifyHistory.ini";
    assert(history::CreateDefaultIni(ini));history::Settings parsed;
    assert(!history::ReadRestartOptions(parsed,ini)&&!parsed.debug_networking);
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugNetworking",L"1",ini.c_str()));
    assert(!history::ReadRestartOptions(parsed,ini)&&parsed.debug_networking);
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugNetworking",nullptr,ini.c_str()));
    assert(history::ReadRestartOptions(parsed,ini)&&!parsed.debug_networking);
    assert(WritePrivateProfileStringW(L"Other",L"Keep",L"\u97f3\u697d",ini.c_str()));
    history::InitSettings(nullptr);assert(!history::GetSettings().debug_networking);
    assert(history::FlushSettings(5000)&&GetPrivateProfileIntW(L"To Disk",L"DebugNetworking",9,ini.c_str())==0);
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugNetworking",L"1",ini.c_str()));
    const auto epoch=history::CaptureEpoch();assert(history::SetLyrics(false)&&history::FlushSettings(5000));
    assert(GetPrivateProfileIntW(L"To Disk",L"DebugNetworking",9,ini.c_str())==1&&history::CaptureEpoch()==epoch);
    wchar_t kept[16];assert(GetPrivateProfileStringW(L"Other",L"Keep",L"",kept,16,ini.c_str())==2&&std::wstring(kept)==L"\u97f3\u697d");
}
void CallbackBoundary() {
    using namespace history;
    decoded_original=NativeParse;decoded_plain_return=0x1111;decoded_compressed_return=0x2222;
    decoded_enabled.store(true);InterlockedExchange(&networking_enabled,1);InterlockedExchange(&debug_enabled,1);InterlockedExchange(&log_enabled,1);
    const std::string pause=R"({"message_id":5,"sent_by_device_id":"PRIVATE_SENDER_A","command":{"endpoint":"pause","logging_params":{"token":"PRIVATE_TOKEN"}}})";
    const auto original_bytes=pause;
    TestString input(pause);const auto before=input;unsigned output=0;
    expected_input=&input;expected_output=&output;
    assert(ForwardDecodedCall(&output,&input,decoded_plain_return)==1&&native_calls==1&&output==0xabcdef);
    assert(GetLastError()==0x10203040&&std::memcmp(&before,&input,sizeof(input))==0&&pause==original_bytes);
    DrainDecodedConnectMessages();assert(Has("action=pause message_id=5 sender=peer-1")&&Has("source=plain")&&Has("execution=unobserved"));
    decoded_lines.clear();assert(ForwardDecodedCall(&output,&input,decoded_compressed_return)==1);
    DrainDecodedConnectMessages();assert(Has("sender=peer-1")&&Has("source=decompressed"));
    const auto count=native_calls;decoded_lines.clear();
    assert(ForwardDecodedCall(&output,&input,0x3333)==1&&native_calls==count+1);DrainDecodedConnectMessages();assert(decoded_lines.empty());
    InterlockedExchange(&debug_enabled,0);assert(ForwardDecodedCall(&output,&input,decoded_plain_return)==1);
    DrainDecodedConnectMessages();assert(decoded_lines.empty());InterlockedExchange(&debug_enabled,1);
    InterlockedExchange(&log_enabled,0);assert(ForwardDecodedCall(&output,&input,decoded_plain_return)==1);
    DrainDecodedConnectMessages();assert(decoded_lines.empty());InterlockedExchange(&log_enabled,1);
    native_result=0;assert(ForwardDecodedCall(&output,&input,decoded_plain_return)==0&&GetLastError()==0x10203040);
    DrainDecodedConnectMessages();assert(Has("json_result=0")&&!Has("action=pause"));native_result=1;
    decoded_lines.clear();input.size=max_connect_message+1;input.capacity=input.size;
    assert(ForwardDecodedCall(&output,&input,decoded_plain_return)==1);DrainDecodedConnectMessages();assert(Has("copy=oversized"));input=before;
    decoded_lines.clear();input.size=16;input.capacity=15;
    assert(ForwardDecodedCall(&output,&input,decoded_plain_return)==1);DrainDecodedConnectMessages();assert(Has("copy=string-layout"));input=before;
    const std::string small="{}";TestString inline_input(small);expected_input=&inline_input;
    decoded_lines.clear();assert(ForwardDecodedCall(&output,&inline_input,decoded_plain_return)==1);
    DrainDecodedConnectMessages();assert(Has("copy=copied summary=unavailable"));expected_input=&input;
    decoded_lines.clear();expected_input=reinterpret_cast<const void*>(1);
    assert(ForwardDecodedCall(&output,expected_input,decoded_plain_return)==1&&GetLastError()==0x10203040);
    DrainDecodedConnectMessages();assert(Has("copy=unreadable"));expected_input=&input;
    decoded_lines.clear();
    for(unsigned i=0;i<decoded_capacity+1;++i)assert(ForwardDecodedCall(&output,&input,decoded_plain_return)==1);
    DrainDecodedConnectMessages();assert(Has("observations omitted=1")&&decoded_count==0);
    for(const auto& slot:decoded_records)for(char byte:slot.bytes)assert(byte==0);
    for(const auto& line:decoded_lines)assert(line.find("PRIVATE_")==std::string::npos);
    decoded_lines.clear();AcquireSRWLockExclusive(&decoded_lock);
    assert(ForwardDecodedCall(&output,&input,decoded_plain_return)==1);
    ReleaseSRWLockExclusive(&decoded_lock);DrainDecodedConnectMessages();assert(Has("observations omitted=1"));
    decoded_lines.clear();decoded_enabled.store(false);
    InterlockedExchange(&networking_enabled,0);
    StartDecodedConnectMessages(reinterpret_cast<HMODULE>(1),true);
    assert(decoded_init.State()==hooks::InitState::Waiting);
    const auto prior=decoded_init.State();StartDecodedConnectMessages(reinterpret_cast<HMODULE>(1),false);
    assert(decoded_init.State()==prior); // Default off never inspects even an invalid module.
}
void RealDiscovery(const wchar_t* path) {
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    assert(file!=INVALID_HANDLE_VALUE);LARGE_INTEGER length{};assert(GetFileSizeEx(file,&length)&&length.QuadPart>0&&length.QuadPart<512LL*1024*1024);
    std::vector<std::uint8_t> raw(std::size_t(length.QuadPart));DWORD read=0;
    assert(ReadFile(file,raw.data(),DWORD(raw.size()),&read,nullptr)&&read==raw.size());CloseHandle(file);
    assert(raw.size()>=sizeof(IMAGE_DOS_HEADER));const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
    assert(dos->e_magic==IMAGE_DOS_SIGNATURE&&dos->e_lfanew>=0&&std::size_t(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS64)<=raw.size());
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(raw.data()+dos->e_lfanew);
    assert(nt->Signature==IMAGE_NT_SIGNATURE&&nt->OptionalHeader.SizeOfImage<512u*1024*1024&&nt->OptionalHeader.SizeOfImage>0);
    std::vector<std::uint8_t> mapped(nt->OptionalHeader.SizeOfImage);
    assert(nt->OptionalHeader.SizeOfHeaders<=raw.size()&&nt->OptionalHeader.SizeOfHeaders<=mapped.size());
    std::memcpy(mapped.data(),raw.data(),nt->OptionalHeader.SizeOfHeaders);
    const auto* sections=IMAGE_FIRST_SECTION(nt);
    assert(reinterpret_cast<const std::uint8_t*>(sections+nt->FileHeader.NumberOfSections)<=raw.data()+raw.size());
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) {
        const auto& s=sections[i];assert(s.PointerToRawData<=raw.size()&&s.SizeOfRawData<=raw.size()-s.PointerToRawData);
        assert(s.VirtualAddress<=mapped.size()&&s.SizeOfRawData<=mapped.size()-s.VirtualAddress);
        std::memcpy(mapped.data()+s.VirtualAddress,raw.data()+s.PointerToRawData,s.SizeOfRawData);
    }
    TestConnectHookDiscovery(mapped.data(),mapped.size());
}
}
namespace history {
void StartLogger(){}
void QueueDiagnostic(const char* text){decoded_lines.emplace_back(text);}
void QueueNetworkingDiagnostic(const char* text) noexcept {decoded_lines.emplace_back(text);}
void LogEvent(LogKind,const char*,const std::string&){}
}
int wmain(int argc,wchar_t** argv) {
    assert(argc==2);TestConnectMessages();TestConnectHookDiscovery();RealDiscovery(argv[1]);DecodedSettings();CallbackBoundary();
    puts("PASS: bounded decoded summaries, synthetic/current-client capability checks, read-only callback/LastError forwarding, redaction, queue limits and optional settings");
    return 0;
}
