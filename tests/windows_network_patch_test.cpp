// Synthetic import tables and an isolated temporary INI; never touches Spotify.
#include "../native/version_proxy.cpp"
#include "../native/history_settings.cpp"
#include <cassert>
#include <vector>

bool PrepareToDiskMenu(HMODULE){return false;}
void FinalizeToDiskMenu(bool){}
void StartAudioHistory(HMODULE,HMODULE){}
extern "C" MH_STATUS WINAPI MH_ApplyQueued(){return MH_OK;}
namespace history {
void StartDecodedConnectMessages(HMODULE,bool) noexcept{}
void DrainDecodedConnectMessages() noexcept{}
bool PrepareMetadataCollector(HMODULE){return false;}
void FinalizeMetadataCollector(bool){}
void StartLogger(){}
void QueueDiagnostic(const char*){}
void LogActivity(const char*,const std::string&){}
}
namespace {
template<class T> void Put(std::vector<BYTE>& image,size_t offset,T value) {
    assert(offset+sizeof(value)<=image.size());memcpy(image.data()+offset,&value,sizeof(value));
}
void Text(std::vector<BYTE>& image,size_t offset,const char* text) {
    assert(offset+strlen(text)+1<=image.size());memcpy(image.data()+offset,text,strlen(text)+1);
}
std::vector<BYTE> Image() {
    std::vector<BYTE> image(4096);
    Put<uint16_t>(image,0,IMAGE_DOS_SIGNATURE);Put<uint32_t>(image,0x3c,0x80);
    Put<uint32_t>(image,0x80,IMAGE_NT_SIGNATURE);Put<uint16_t>(image,0x84,IMAGE_FILE_MACHINE_AMD64);
    Put<uint16_t>(image,0x94,240);Put<uint16_t>(image,0x98,IMAGE_NT_OPTIONAL_HDR64_MAGIC);
    Put<uint32_t>(image,0x98+56,4096);Put<uint32_t>(image,0x98+108,16);
    Put<uint32_t>(image,0x98+120,0x200);Put<uint32_t>(image,0x98+124,40);
    Put<uint32_t>(image,0x200,0x300);Put<uint32_t>(image,0x20c,0x280);Put<uint32_t>(image,0x210,0x380);
    Text(image,0x280,"ole32.dll");Put<uint64_t>(image,0x300,0x400);Put<uint64_t>(image,0x380,0x11111111);
    Text(image,0x402,"CoCreateInstance");
    Put<uint32_t>(image,0x98+112+13*8,0x500);Put<uint32_t>(image,0x98+116+13*8,64);
    Put<uint32_t>(image,0x500,1);Put<uint32_t>(image,0x504,0x580);Put<uint32_t>(image,0x50c,0x680);Put<uint32_t>(image,0x510,0x600);
    Text(image,0x580,"ole32.dll");Put<uint64_t>(image,0x600,0x700);Put<uint64_t>(image,0x680,0x22222222);
    Text(image,0x702,"CoCreateInstance");return image;
}
void ImportGuard() {
    auto image=Image();const auto untouched=image;const auto module=reinterpret_cast<HMODULE>(image.data());
    assert(!history::Settings{}.network_patch);
    // Disabled means no PE inspection, no IAT writes and no retry-state changes.
    StartConnectivityHook(reinterpret_cast<HMODULE>(1),false);
    StartConnectivityHook(module,false);
    assert(image==untouched&&connectivity_init.State()==hooks::InitState::Waiting&&table_count==0);
    StartConnectivityHook(module,true);
    assert(connectivity_init.State()==hooks::InitState::Active);
    void* replacement=reinterpret_cast<void*>(RepairCoCreateInstance);
    assert(*reinterpret_cast<void**>(image.data()+0x380)==replacement);
    assert(*reinterpret_cast<void**>(image.data()+0x680)==replacement);
    // Enabled mode still repairs a delay resolver changing an active slot.
    *reinterpret_cast<void**>(image.data()+0x680)=reinterpret_cast<void*>(0x22222222);
    StartConnectivityHook(module,true);
    assert(*reinterpret_cast<void**>(image.data()+0x680)==replacement);
}
void Settings() {
    wchar_t executable[32768];assert(GetModuleFileNameW(nullptr,executable,32768));
    const std::wstring exe=executable,path=exe.substr(0,exe.find_last_of(L'\\'))+L"\\SpotifyHistory.ini";
    assert(history::CreateDefaultIni(path));
    assert(GetPrivateProfileIntW(L"To Disk",L"NetworkPatch",9,path.c_str())==0);
    history::Settings parsed;
    assert(!history::ReadRestartOptions(parsed,path)&&!parsed.network_patch);
    assert(WritePrivateProfileStringW(L"To Disk",L"NetworkPatch",L"1",path.c_str()));
    assert(!history::ReadRestartOptions(parsed,path)&&parsed.network_patch);
    assert(WritePrivateProfileStringW(L"To Disk",L"NetworkPatch",L"0",path.c_str()));
    assert(!history::ReadRestartOptions(parsed,path)&&!parsed.network_patch);
    assert(WritePrivateProfileStringW(L"To Disk",L"NetworkPatch",nullptr,path.c_str()));
    assert(history::ReadRestartOptions(parsed,path)&&!parsed.network_patch);
    assert(WritePrivateProfileStringW(L"Other",L"Keep",L"\u97f3\u697d",path.c_str()));
    history::InitSettings(nullptr);
    assert(!history::GetSettings().network_patch&&history::GetSettings().downloads);
    assert(history::FlushSettings(5000));
    assert(GetPrivateProfileIntW(L"To Disk",L"NetworkPatch",9,path.c_str())==0);
    // An external restart-only edit must survive an unrelated menu save.
    assert(WritePrivateProfileStringW(L"To Disk",L"NetworkPatch",L"1",path.c_str()));
    const auto epoch=history::CaptureEpoch();assert(history::SetLyrics(false)&&history::FlushSettings(5000));
    assert(GetPrivateProfileIntW(L"To Disk",L"NetworkPatch",9,path.c_str())==1);
    assert(history::CaptureEpoch()==epoch&&history::GetSettings().downloads);
    wchar_t kept[16];assert(GetPrivateProfileStringW(L"Other",L"Keep",L"",kept,16,path.c_str())==2&&std::wstring(kept)==L"\u97f3\u697d");
}
}
int wmain() {
    ImportGuard();Settings();
    puts("PASS: default/missing/explicit network setting, Unicode preservation, independent capture, disabled IAT bypass and opt-in import hooks");
    return 0;
}
