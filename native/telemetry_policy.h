#pragma once
#include <array>
#include <string_view>
namespace privacy {
enum class Rule {Allow,Gabo,Logging,Sentry,Playback,Growth,Crash,TrackError,AITelemetry,Count};
inline const char* RuleName(Rule rule) noexcept {
    switch(rule){case Rule::Gabo:return "gabo";case Rule::Logging:return "logging";case Rule::Sentry:return "sentry";case Rule::Playback:return "playback";case Rule::Growth:return "growth";case Rule::Crash:return "crash";case Rule::TrackError:return "track_error";case Rule::AITelemetry:return "ai_telemetry";default:return "none";}
}
namespace detail {
inline wchar_t Lower(wchar_t c) noexcept {return c>=L'A'&&c<=L'Z'?c+(L'a'-L'A'):c;}
inline bool Equal(std::wstring_view a,std::wstring_view b) noexcept {
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i)if(Lower(a[i])!=b[i])return false;
    return true;
}
// Regional client hosts are a single DNS label ending in -spclient.spotify.com.
// Do not accept arbitrary subdomains, suffix lookalikes or malformed labels.
inline bool Spclient(std::wstring_view host) noexcept {
    if(Equal(host,L"spclient.wg.spotify.com"))return true;
    constexpr std::wstring_view suffix=L"-spclient.spotify.com";
    if(host.size()<=suffix.size()||!Equal(host.substr(host.size()-suffix.size()),suffix))return false;
    const auto region=host.substr(0,host.size()-suffix.size());
    if(region.size()+8>63||region.front()==L'-'||region.back()==L'-')return false;
    for(auto c:region) {c=Lower(c);if(!((c>=L'a'&&c<=L'z')||(c>=L'0'&&c<=L'9')||c==L'-'))return false;}
    return true;
}
inline int Hex(wchar_t c) noexcept {
    c=Lower(c);return c>=L'0'&&c<=L'9'?int(c-L'0'):c>=L'a'&&c<=L'f'?int(c-L'a'+10):-1;
}
inline bool Route(std::wstring_view path,std::wstring_view root) noexcept {
    return path==root||(path.size()>root.size()&&path.substr(0,root.size())==root&&path[root.size()]==L'/');
}
}
// Fixed, audited endpoints only. No bodies, regex engine, allocations or mutable
// user rules in the callback. Unrecognized/malformed URLs are outside coverage.
inline Rule Classify(std::wstring_view url) noexcept {
    if(url.empty()||url.size()>4096)return Rule::Allow;
    for(wchar_t c:url)if(c<=L' '||c==127)return Rule::Allow;
    const auto colon=url.find(L"://");if(colon==url.npos)return Rule::Allow;
    const auto scheme=url.substr(0,colon);
    const bool http=detail::Equal(scheme,L"http")||detail::Equal(scheme,L"https");
    if(!http&&!detail::Equal(scheme,L"sp"))return Rule::Allow;
    const auto start=colon+3,stop=url.find_first_of(L"/\\?#",start);
    auto host=url.substr(start,stop==url.npos?url.size()-start:stop-start);
    const auto user=host.rfind(L'@');if(user!=host.npos)host.remove_prefix(user+1);
    const auto port=host.find(L':');
    if(port!=host.npos){
        const auto text=host.substr(port+1);if(text.empty()||text.size()>5)return Rule::Allow;
        unsigned value=0;for(wchar_t c:text){if(c<L'0'||c>L'9')return Rule::Allow;value=value*10+unsigned(c-L'0');}
        if(!value||value>65535)return Rule::Allow;
        host=host.substr(0,port);
    }
    if(!host.empty()&&host.back()==L'.')host.remove_suffix(1);
    const bool spclient=http&&detail::Spclient(host);
    const bool logging=detail::Equal(scheme,L"sp")&&detail::Equal(host,L"logging");
    const bool sentry=http&&detail::Equal(host,L"o22381.ingest.sentry.io");
    const bool growth=http&&detail::Equal(host,L"www.spotify.com");
    const bool crash=http&&detail::Equal(host,L"crashdump.spotify.com");
    if(!spclient&&!logging&&!sentry&&!growth&&!crash)return Rule::Allow;
    auto raw=stop==url.npos?std::wstring_view{}:url.substr(stop);
    raw=raw.substr(0,raw.find_first_of(L"?#"));
    std::array<wchar_t,4097> decoded{},canonical{};size_t count=0;
    for(size_t i=0;i<raw.size();++i){
        wchar_t c=raw[i];
        if(c==L'%'&&i+2<raw.size()){
            const int a=detail::Hex(raw[i+1]),b=detail::Hex(raw[i+2]);
            if(a>=0&&b>=0){const wchar_t v=wchar_t(a*16+b);
                if((v>=L'a'&&v<=L'z')||(v>=L'A'&&v<=L'Z')||(v>=L'0'&&v<=L'9')||v==L'-'||v==L'.'||v==L'_'||v==L'~'||v==L'/'||v==L'\\'){c=v;i+=2;}
            }
        }
        decoded[count++]=c==L'\\'?L'/':c;
    }
    // Dot segments must be resolved before deciding which service receives it.
    size_t used=0;const std::wstring_view path(decoded.data(),count);
    for(size_t begin=0;begin<=count;){
        auto end=path.find(L'/',begin);if(end==path.npos)end=count;
        const auto segment=path.substr(begin,end-begin);
        if(segment==L"..") {while(used&&canonical[used-1]!=L'/')--used;if(used)--used;}
        else if(!segment.empty()&&segment!=L"."){
            canonical[used++]=L'/';for(wchar_t c:segment)canonical[used++]=c;
        }
        if(end==count)break;
        begin=end+1;
    }
    const std::wstring_view normalized(canonical.data(),used);
    if(spclient&&detail::Route(normalized,L"/gabo-receiver-service"))return Rule::Gabo;
    if(spclient&&(detail::Route(normalized,L"/melody/v1/msg")||detail::Route(normalized,L"/melody/unauth/v1/msg")))return Rule::Playback;
    if(spclient&&normalized==L"/track-error/v1/errors")return Rule::TrackError;
    if(spclient&&(normalized==L"/v1/telemetry/sandbox"||normalized==L"/v1/telemetry/turn-termination"))return Rule::AITelemetry;
    if(growth&&detail::Route(normalized,L"/api/growth-events"))return Rule::Growth;
    if(crash)return Rule::Crash;
    if(logging&&(detail::Route(normalized,L"/v1")||detail::Route(normalized,L"/v2")||detail::Route(normalized,L"/v3")))return Rule::Logging;
    if(sentry&&detail::Route(normalized,L"/api/114855"))return Rule::Sentry;
    return Rule::Allow;
}
}
