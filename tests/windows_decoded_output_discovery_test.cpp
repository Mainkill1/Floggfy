#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../native/spotify_hook_discovery.h"
#include <cassert>
#include <cstdio>
#include <cwchar>

namespace {
const char* Name(spotify::DiscoveryResult result) {
    switch(result) {
    case spotify::DiscoveryResult::Found:return "found";
    case spotify::DiscoveryResult::InvalidImage:return "invalid-image";
    case spotify::DiscoveryResult::MissingTarget:return "missing-target";
    case spotify::DiscoveryResult::AmbiguousTarget:return "ambiguous-target";
    case spotify::DiscoveryResult::InconsistentLayout:return "inconsistent-layout";
    }
    return "unknown";
}
}

int wmain(int argc,wchar_t** argv) {
    const bool probe=argc==3&&!std::wcscmp(argv[1],L"--probe");
    if((!probe&&argc!=2)||(probe&&argc!=3)){std::puts("SKIP: decoded-output current-client discovery (no binary supplied)");return 0;}
    HMODULE module=LoadLibraryExW(argv[probe?2:1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    assert(module);auto* base=reinterpret_cast<unsigned char*>(module);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(probe) {
        spotify::HookTargets capture{};spotify::AacHookTargets aac{};
        spotify::DecodedOutputTarget decoded{};spotify::DecoderOwnershipLayout ownership{};
        spotify::ConnectDecodeTargets connect{};
        const auto capture_result=spotify::DiscoverHookTargets(base,nt->OptionalHeader.SizeOfImage,capture);
        const auto aac_result=spotify::DiscoverAacHookTargets(base,nt->OptionalHeader.SizeOfImage,aac);
        const auto decoded_result=spotify::DiscoverDecodedOutputTarget(base,nt->OptionalHeader.SizeOfImage,decoded);
        const auto ownership_result=spotify::DiscoverDecoderOwnershipLayout(base,nt->OptionalHeader.SizeOfImage,ownership);
        const auto connect_result=spotify::DiscoverConnectDecodeTargets(base,nt->OptionalHeader.SizeOfImage,connect);
        std::printf("capture=%s ogg=0x%x flac_init=0x%x aac=%s decoded=%s decoded_rva=0x%x ownership=%s ogg_delta=%d flac_delta=%d aac_verified=%d connect=%s\n",
            Name(capture_result),spotify::TargetRva(capture,spotify::TargetKind::OggPageSeek),
            spotify::TargetRva(capture,spotify::TargetKind::FlacInit),Name(aac_result),
            Name(decoded_result),decoded.rva,Name(ownership_result),ownership.ogg_capture_to_decoder,
            ownership.flac_capture_to_decoder,ownership.aac_verified,Name(connect_result));
        FreeLibrary(module);return capture_result==spotify::DiscoveryResult::Found?0:4;
    }
    spotify::DecodedOutputTarget target{};
    assert(spotify::DiscoverDecodedOutputTarget(base,nt->OptionalHeader.SizeOfImage,target)==spotify::DiscoveryResult::Found);
    spotify::DecoderOwnershipLayout ownership{};
    assert(spotify::DiscoverDecoderOwnershipLayout(base,nt->OptionalHeader.SizeOfImage,ownership)==spotify::DiscoveryResult::Found);
    assert(target.rva&&ownership.decoded_output==target.rva);
    std::printf("PASS: current-client decoder ownership decoded_rva=0x%x ogg_delta=%d flac_delta=%d aac_delta=%d aac_verified=%d\n",
        ownership.decoded_output,ownership.ogg_capture_to_decoder,ownership.flac_capture_to_decoder,
        ownership.aac_capture_to_decoder,ownership.aac_verified);
    FreeLibrary(module);return 0;
}
