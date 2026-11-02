#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../native/spotify_hook_discovery.h"
#include <cassert>
#include <cstdio>

int wmain(int argc,wchar_t** argv) {
    if(argc!=2){std::puts("SKIP: decoded-output current-client discovery (no binary supplied)");return 0;}
    HMODULE module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    assert(module);auto* base=reinterpret_cast<unsigned char*>(module);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
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
