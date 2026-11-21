#pragma once
#include "rich_metadata.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <optional>
namespace history {
struct ClientPlaybackRecord {
    RichMetadata metadata;
    double position=0,sampled_ms=0;
    bool playing=false,buffering=false;
    bool explicit_control=false,prior_control=false;
    unsigned speed=1;
    int local=-1;
    std::string instance;
};
enum class ClientInterstitialKind { None,Interstitial,Advertisement };
inline bool ParseClientActiveWithoutUri(const std::string& input,
                                        ClientInterstitialKind* kind=nullptr) noexcept {
    ClientInterstitialKind parsed=ClientInterstitialKind::None;
    if(input=="v=1&unavailable=track-unavailable&active=1"||
       input=="v=1&unavailable=track-unavailable&active=1&kind=interstitial")
        parsed=ClientInterstitialKind::Interstitial;
    else if(input=="v=1&unavailable=track-unavailable&active=1&kind=advertisement")
        parsed=ClientInterstitialKind::Advertisement;
    else return false;
    if(kind)*kind=parsed;
    return true;
}
namespace client_playback_detail {
inline bool ReadPair(std::string_view input, size_t& at,
                     std::string_view& key, std::string_view& value) {
    const auto separator=input.find('&',at);
    const auto end=separator==input.npos ? input.size() : separator;
    const auto pair=input.substr(at,end-at);
    const auto equals=pair.find('=');
    if(equals==pair.npos)return false;
    key=pair.substr(0,equals);
    value=pair.substr(equals+1);
    at=end+1;
    return true;
}

template<size_t N>
struct Fields {
    const std::array<std::string_view,N>& names;
    std::array<std::optional<std::string_view>,N> values{};
    explicit Fields(const std::array<std::string_view,N>& keys) : names(keys) {}

    size_t Index(std::string_view key) const {
        return static_cast<size_t>(std::find(names.begin(),names.end(),key)-names.begin());
    }
    std::string_view operator[](size_t index) const { return values[index].value_or(std::string_view{}); }
    bool Has(size_t index) const { return values[index].has_value(); }
    bool Store(size_t index,std::string_view value) {
        if(Has(index))return false;
        values[index]=value;
        return true;
    }
    bool Flag(size_t index,bool& flag,bool required=false) const {
        if(!Has(index))return !required;
        flag=values[index]=="1";
        return flag || values[index]=="0";
    }
};

inline bool Instance(std::string_view encoded,std::string& instance) {
    return encoded.size()<=3*128 && DecodeMetadataText(encoded,instance) && instance.size()<=128 &&
        std::all_of(instance.begin(),instance.end(),[](unsigned char c){return c>32 && c<127;});
}
inline bool Number(std::string_view text,double& number) {
    if(text.empty())return false;
    // strtod needs a terminated field and keeps the existing numeric grammar.
    const std::string field(text);
    char* end=nullptr;number=std::strtod(field.c_str(),&end);
    return !*end && std::isfinite(number);
}
}
inline bool ParseClientPlayback(const std::string& input,ClientPlaybackRecord& out,std::string& error) {
    const auto fail=[&](const char* reason){error=reason;return false;};
    if(input.size()>16384)return fail("client-payload-too-large");
    if(input.find('\0')!=std::string::npos)return fail("client-invalid-nul");
    enum Field { Position, SampledMs, Paused, Buffering, Local, InstanceId, Speed, Controlled, PriorControl, Count };
    static constexpr std::array<std::string_view,Count> names{
        "position","sampled_ms","paused","buffering","local","instance","speed","controlled","prior_control"};
    client_playback_detail::Fields timeline(names);
    std::string metadata;
    for(size_t at=0;at<input.size();) {
        const auto start=at;
        std::string_view key,value;
        if(!client_playback_detail::ReadPair(input,at,key,value))return fail("client-invalid-pair");
        const auto index=timeline.Index(key);
        if(index<Count) {
            if(!timeline.Store(index,value))return fail("client-duplicate-field");
        } else {
            if(!metadata.empty())metadata+='&';
            metadata.append(input,start,at-start-1);
        }
    }
    ClientPlaybackRecord record;
    if(!ParseRichMetadata(metadata,record.metadata,error))return false;
    if(!ValidSpotifyMediaUri(record.metadata.uri))return fail("client-invalid-uri");
    if(!client_playback_detail::Number(timeline[Position],record.position)||record.position<0||record.position>record.metadata.duration)return fail("client-invalid-position");
    if(!client_playback_detail::Number(timeline[SampledMs],record.sampled_ms)||record.sampled_ms<=0)return fail("client-invalid-clock");
    bool paused=false;
    if(!timeline.Flag(Paused,paused,true)||!timeline.Flag(Buffering,record.buffering,true))return fail("client-invalid-playback-flags");
    record.playing=!paused;
    if(!timeline.Flag(Controlled,record.explicit_control))return fail("client-invalid-control-flag");
    if(!timeline.Flag(PriorControl,record.prior_control))return fail("client-invalid-prior-control-flag");
    if(timeline.Has(Speed)) {
        const auto speed=timeline[Speed];
        if(speed=="1")record.speed=1;else if(speed=="2")record.speed=2;
        else if(speed=="4")record.speed=4;else if(speed=="8")record.speed=8;
        else if(speed=="10")record.speed=10;else return fail("client-invalid-speed");
    }
    if(timeline[Local]=="1")record.local=1;
    else if(timeline[Local]=="0")record.local=0;
    else if(timeline[Local]!="unknown")return fail("client-invalid-device-flag");
    if(!client_playback_detail::Instance(timeline[InstanceId],record.instance))return fail("client-invalid-instance");
    out=std::move(record);error.clear();return true;
}
struct ClientTransition {std::string uri,instance;double sampled_ms=0,transition_ms=0,position=-1;bool explicit_skip=false,explicit_control=false,control_monitoring=false,natural=false,pending=false,terminal=false;};
inline bool ParseClientTransition(const std::string& input,ClientTransition& out) {
    if(input.size()>16384||input.find('\0')!=std::string::npos)return false;
    enum Field { Version, Blocked, Uri, InstanceId, SampledMs, TransitionMs, PositionMs,
                 Manual, Controlled, Monitoring, Natural, Pending, Terminal, Count };
    static constexpr std::array<std::string_view,Count> names{
        "v","blocked","uri","instance","sampled_ms","transition_ms","position_ms",
        "manual","controlled","monitoring","natural","pending","terminal"};
    client_playback_detail::Fields values(names);
    for(size_t at=0;at<input.size();) {
        std::string_view key,value;
        if(!client_playback_detail::ReadPair(input,at,key,value))return false;
        const auto index=values.Index(key);
        if(index==Count||!values.Store(index,value))return false;
    }
    if(values[Version]!="1"||values[Blocked]!="buffering")return false;
    ClientTransition record;
    // Preserve the aggregate encoded identity/clock budget of the original wire parser.
    if(values[Uri].size()+values[InstanceId].size()+values[SampledMs].size()>16384-133)return false;
    if(!DecodeMetadataText(values[Uri],record.uri) || !ValidSpotifyMediaUri(record.uri) ||
       !client_playback_detail::Instance(values[InstanceId],record.instance) ||
       !client_playback_detail::Number(values[SampledMs],record.sampled_ms) || record.sampled_ms<=0 ||
       !client_playback_detail::Number(values[TransitionMs],record.transition_ms) || record.transition_ms<=0 ||
       record.transition_ms>record.sampled_ms+250)return false;
    if(values.Has(PositionMs)) {
        if(!client_playback_detail::Number(values[PositionMs],record.position))return false;
        record.position/=1000;
        if(record.position<0||record.position>86400)return false;
    }
    if(!values.Flag(Manual,record.explicit_skip) || !values.Flag(Controlled,record.explicit_control) ||
       !values.Flag(Monitoring,record.control_monitoring,true) || !values.Flag(Natural,record.natural) ||
       !values.Flag(Pending,record.pending) || !values.Flag(Terminal,record.terminal))return false;
    if(record.natural && record.pending)return false;
    if(record.terminal && (record.uri.rfind("spotify:episode:",0)!=0 || record.instance.empty() ||
       !record.control_monitoring || record.natural || record.pending || record.explicit_skip || record.explicit_control))return false;
    out=std::move(record);return true;
}

}
