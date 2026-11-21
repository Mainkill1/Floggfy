#include "../native/telemetry/telemetry_policy.h"
#include <cassert>
#include <string>
#include <cstdio>
using privacy::Classify;using privacy::Rule;
int main(){
    assert(Classify(L"https://gew1-spclient.spotify.com/gabo-receiver-service/v3/events")==Rule::Gabo);
    assert(Classify(L"sp://logging/v1/events")==Rule::Logging);
    assert(Classify(L"sp://logging/v2/events")==Rule::Logging);
    for(const auto* url:{L"https://spclient.wg.spotify.com/gabo-receiver-service/v3/events",L"HTTPS://SPCLIENT.WG.SPOTIFY.COM.:443/gabo-receiver-service",L"https://spclient.wg.spotify.com/%67abo-receiver-service/v3/events?token=private",L"https://spclient.wg.spotify.com/a/../gabo-receiver-service/v3/events"})assert(Classify(url)==Rule::Gabo);
    assert(Classify(L"sp://logging/v3/events")==Rule::Logging);
    assert(Classify(L"https://o22381.ingest.sentry.io/api/114855/envelope/?sentry_key=private")==Rule::Sentry);
    for(const auto* url:{L"https://spclient.wg.spotify.com/metadata/4/track/id",L"https://spclient.wg.spotify.com/metadata/4/album/id",L"https://spclient.wg.spotify.com/color-lyrics/v2/track/id",L"https://spclient.wg.spotify.com/ads/a",L"https://spclient.wg.spotify.com/desktop-update/v1",L"https://accounts.spotify.com/login",L"https://audio-fa.scdn.co/audio/file",L"https://spclient.wg.spotify.com/gabo-receiver-service-other",L"https://spclient.wg.spotify.com/gabo-receiver-service/../metadata/4/track/id",L"https://spclient.wg.spotify.com/metadata?q=/gabo-receiver-service/",L"https://spclient.wg.spotify.com.evil.invalid/gabo-receiver-service/",L"https://spclient.wg.spotify.com@evil.invalid/gabo-receiver-service/",L"https://o22381.ingest.sentry.io/api/999/envelope/",L"https://other.ingest.sentry.io/api/114855/envelope/",L"sp://logging/v30/events",L"/gabo-receiver-service/v3/events",L"https://spclient.wg.spotify.com:bad/gabo-receiver-service/",L"https://spclient.wg.spotify.com:65536/gabo-receiver-service/"})assert(Classify(url)==Rule::Allow);
    struct Category {const wchar_t* path;Rule rule;};
    for(const auto& category:std::array<Category,6>{{
        {L"/gabo-receiver-service/v3/events",Rule::Gabo},
        {L"/melody/v1/msg",Rule::Playback},
        {L"/melody/unauth/v1/msg",Rule::Playback},
        {L"/track-error/v1/errors",Rule::TrackError},
        {L"/v1/telemetry/sandbox",Rule::AITelemetry},
        {L"/v1/telemetry/turn-termination",Rule::AITelemetry}}}) {
        for(const wchar_t* host:{L"spclient.wg.spotify.com",L"gew1-spclient.spotify.com",L"guc3-spclient.spotify.com",L"GEW1-SPCLIENT.SPOTIFY.COM.:443"})
            assert(Classify(std::wstring(L"https://")+host+category.path+L"?test=1")==category.rule);
        for(const wchar_t* host:{L"spclient.wg.spotify.com.evil",L"gew1-spclient.spotify.com.evil",L"evil.gew1-spclient.spotify.com",L"-spclient.spotify.com",L"-gew1-spclient.spotify.com",L"gew1--spclient.spotify.com",L"gew1_spclient.spotify.com",L"gew1.-spclient.spotify.com",L"gew1-spclient.spotify.com@evil.invalid",L"spotify.com"})
            assert(Classify(std::wstring(L"https://")+host+category.path)==Rule::Allow);
    }
    assert(Classify(L"https://www.spotify.com/api/growth-events?x=1")==Rule::Growth);
    assert(Classify(L"https://www.spotify.com/api/growth-events/batch")==Rule::Growth);
    assert(Classify(L"https://gew1-spclient.spotify.com/melody/v1/msg/batch")==Rule::Playback);
    assert(Classify(L"https://gew1-spclient.spotify.com/melody/v1/msg/media_playback_event_v2")==Rule::Playback);
    assert(Classify(L"https://gew1-spclient.spotify.com/melody/unauth/v1/msg/batch")==Rule::Playback);
    assert(Classify(L"https://crashdump.spotify.com/")==Rule::Crash);
    assert(Classify(L"https://crashdump.spotify.com:443/report?id=1")==Rule::Crash);
    for(const wchar_t* url:{L"https://www.spotify.com/api/growth-events-other",L"https://www.spotify.com.evil/api/growth-events",L"https://spotify.com/api/growth-events",L"https://crashdump.spotify.com.evil/",L"https://evil.crashdump.spotify.com/",L"sp://logging/v10/events",L"sp://logging/v20/events",L"hm://event-service/v1/events"})assert(Classify(url)==Rule::Allow);
    for(const wchar_t* path:{L"/melody/v1/license_url",L"/melody/v1/msg-other",L"/melody/unauth/v1/license_url",L"/track-error/v1/errors-other",L"/track-error/v1/errors/other",L"/v1/telemetry/sandbox-other",L"/v1/telemetry/sandbox/other",L"/v1/telemetry/turn-termination-other",L"/v1/telemetry/turn-termination/other",L"/metadata/4/track/id",L"/color-lyrics/v2/track/id",L"/desktop-update/v1"})
        assert(Classify(std::wstring(L"https://gew1-spclient.spotify.com")+path)==Rule::Allow);
    assert(Classify(L"https://gew1-spclient.spotify.com/melody/v1/msg/../license_url")==Rule::Allow);
    assert(Classify(L"https://gew1-spclient.spotify.com/melody/v1/%6dsg")==Rule::Playback);
    assert(std::string(privacy::RuleName(Rule::Gabo))=="gabo");
    assert(std::string(privacy::RuleName(Rule::TrackError))=="track_error");
    assert(std::string(privacy::RuleName(Rule::AITelemetry))=="ai_telemetry");
    for(const wchar_t* host:{
        L"aet.spotify.com",
        L"lciapi.ninthdecimal.com",
        L"tracker.samplicio.us",
        L"tpt.dotomi.com",
        L"aes.us-east.3px.axp.amazon-adsystem.com",
        L"aax-events-cell02-cf.us-east.3px.axp.amazon-adsystem.com"}) {
        const std::wstring exact=std::wstring(L"https://")+host;
        assert(std::string(privacy::RuleName(Classify(exact+L":443/event")))=="queued_telemetry");
        assert(Classify(exact+L".invalid/event")==Rule::Allow);
        assert(Classify(std::wstring(L"https://child.")+host+L"/event")==Rule::Allow);
        assert(Classify(exact+L"@unrelated.example/event")==Rule::Allow);
        assert(Classify(std::wstring(L"sp://")+host+L"/event")==Rule::Allow);
    }
    const wchar_t nul[]=L"https://spclient.wg.spotify.com\0.evil.invalid/gabo-receiver-service/";
    assert(Classify({nul,sizeof(nul)/sizeof(*nul)-1})==Rule::Allow);
    std::wstring long_url(5000,L'x');assert(Classify(long_url)==Rule::Allow);
    puts("PASS: telemetry host/path boundaries, canonical paths and functional Spotify routes");
}
