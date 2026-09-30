#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "existing_quality.h"
#include "history_settings.h"
#include "ogg_history_core.h"
#include <cstring>
namespace history {
bool ReadQuality(const std::wstring& path,Quality& out) {
    auto name=WindowsPath(path);
    HANDLE file=CreateFileW(name.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    unsigned char b[65307]; DWORD n=0;
    bool ok=ReadFile(file,b,sizeof(b),&n,nullptr)!=0; CloseHandle(file);
    if(!ok || n<27) return false;
    if(!std::memcmp(b,"OggS",4)) {
        size_t header=27+b[26]; if(header>n) return false;
        size_t body=0; for(size_t i=27;i<header;i++) body+=b[i];
        if(header+body>n) return false;
        Page p; if(!ParsePage(b,header+body,p) || !p.vorbis_start) return false;
        out={Codec::Vorbis,p.rate,p.channels,0,p.bitrate}; return true;
    }
    if(n>=42 && !std::memcmp(b,"fLaC",4) && (b[4]&127)==0 && b[5]==0 && b[6]==0 && b[7]==34) {
        uint64_t packed=0; for(unsigned i=18;i<26;i++) packed=(packed<<8)|b[i];
        out={Codec::Flac,unsigned(packed>>44),unsigned((packed>>41)&7)+1,unsigned((packed>>36)&31)+1,0};
        return out.rate>0;
    }
    return false;
}
SaveDecision DecideSave(const std::wstring& path,const Quality& incoming) {
    auto name=WindowsPath(path); DWORD a=GetFileAttributesW(name.c_str());
    if(a==INVALID_FILE_ATTRIBUTES) return GetLastError()==ERROR_FILE_NOT_FOUND || GetLastError()==ERROR_PATH_NOT_FOUND ? SaveDecision::NewFile : SaveDecision::Skip;
    Quality existing;
    if((a&FILE_ATTRIBUTE_DIRECTORY) || !ReadQuality(path,existing)) return SaveDecision::Skip;
    return HigherQuality(incoming,existing) ? SaveDecision::Upgrade : SaveDecision::Skip;
}
}
