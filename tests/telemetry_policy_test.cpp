#include "../native/telemetry_policy.h"
#include <cassert>
#include <string>
#include <cstdio>
using privacy::Classify;using privacy::Rule;
int main(){
    for(const auto* url:{L"https://spclient.wg.spotify.com/gabo-receiver-service/v3/events",L"HTTPS://SPCLIENT.WG.SPOTIFY.COM.:443/gabo-receiver-service",L"https://spclient.wg.spotify.com/%67abo-receiver-service/v3/events?token=private",L"https://spclient.wg.spotify.com/a/../gabo-receiver-service/v3/events"})assert(Classify(url)==Rule::Gabo);
    assert(Classify(L"sp://logging/v3/events")==Rule::Logging);
    assert(Classify(L"https://o22381.ingest.sentry.io/api/114855/envelope/?sentry_key=private")==Rule::Sentry);
    for(const auto* url:{L"https://spclient.wg.spotify.com/metadata/4/track/id",L"https://spclient.wg.spotify.com/metadata/4/album/id",L"https://spclient.wg.spotify.com/color-lyrics/v2/track/id",L"https://spclient.wg.spotify.com/ads/a",L"https://spclient.wg.spotify.com/desktop-update/v1",L"https://accounts.spotify.com/login",L"https://audio-fa.scdn.co/audio/file",L"https://spclient.wg.spotify.com/gabo-receiver-service-other",L"https://spclient.wg.spotify.com/gabo-receiver-service/../metadata/4/track/id",L"https://spclient.wg.spotify.com/metadata?q=/gabo-receiver-service/",L"https://spclient.wg.spotify.com.evil.invalid/gabo-receiver-service/",L"https://spclient.wg.spotify.com@evil.invalid/gabo-receiver-service/",L"https://o22381.ingest.sentry.io/api/999/envelope/",L"https://other.ingest.sentry.io/api/114855/envelope/",L"sp://logging/v30/events",L"/gabo-receiver-service/v3/events",L"https://spclient.wg.spotify.com:bad/gabo-receiver-service/",L"https://spclient.wg.spotify.com:65536/gabo-receiver-service/"})assert(Classify(url)==Rule::Allow);
    const wchar_t nul[]=L"https://spclient.wg.spotify.com\0.evil.invalid/gabo-receiver-service/";
    assert(Classify({nul,sizeof(nul)/sizeof(*nul)-1})==Rule::Allow);
    std::wstring long_url(5000,L'x');assert(Classify(long_url)==Rule::Allow);
    puts("PASS: telemetry host/path boundaries, canonical paths and functional Spotify routes");
}
