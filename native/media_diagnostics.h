#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
namespace history {
// Static labels and numeric values only: no titles, application IDs or paths.
struct MediaDiagnostic {
    const char* stage="observer-starting";
    const char* reason="pending";
    const char* source="windows";
    const char* client_reason="not-discovered";
    int32_t hresult=0;
    unsigned sessions=0,candidates=0;
    int status=-1;
    double position=0,duration=0;
    bool title_present=false;
};
inline std::string DescribeMediaDiagnostic(const MediaDiagnostic& value) {
    char line[384];
    std::snprintf(line,sizeof(line),
        "media-session stage=%s reason=%s hr=0x%08lx sessions=%u candidates=%u status=%d pos=%.3f/%.3f title_present=%d source=%s client=%s",
        value.stage,value.reason,static_cast<unsigned long>(uint32_t(value.hresult)),
        value.sessions,value.candidates,value.status,value.position,value.duration,value.title_present,value.source,value.client_reason);
    return line;
}
inline const char* MediaRecoveryHint(const MediaDiagnostic& value) noexcept {
    if(std::strcmp(value.source,"spotify")==0) {
        if(std::strcmp(value.reason,"remote-playback")==0)return "select this computer as the Spotify playback device, then replay from the beginning";
        if(std::strcmp(value.reason,"client-buffering")==0)return "wait for Spotify buffering to finish, then replay from the beginning";
        return "restore Spotify app playback observations, then replay from the beginning";
    }
    if(std::strcmp(value.reason,"no-spotify-session")==0)
        return "check Spotify Settings > Display > Show desktop overlay when using media keys; enable it if disabled, then replay from the beginning";
    return "restore Windows media-session availability, then replay from the beginning";
}
class MediaDiagnosticReporter {
    bool reported_=false,healthy_=false;
    const char* source_="";
    double last_=0;
public:
    // Report recovery immediately, but bound persistent/alternating failures.
    bool Due(const MediaDiagnostic& value,double now) noexcept {
        const bool healthy=std::strcmp(value.stage,"ready")==0;
        if(reported_ && healthy==healthy_ && std::strcmp(source_,value.source)==0 && (healthy || now-last_<30))return false;
        reported_=true;healthy_=healthy;source_=value.source;last_=now;return true;
    }
};
}
