#pragma once
#include "rich_metadata.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
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
inline bool ClientInstance(const std::string& encoded,std::string& instance) {
    return encoded.size()<=3*128 && DecodeMetadataText(encoded,instance) && instance.size()<=128 &&
        std::all_of(instance.begin(),instance.end(),[](unsigned char c){return c>32 && c<127;});
}
inline bool ClientNumber(const std::string& text,double& number) {
    if(text.empty())return false;
    char* end=nullptr;number=std::strtod(text.c_str(),&end);
    return !*end && std::isfinite(number);
}
inline bool ClientFlag(const std::map<std::string,std::string>& fields,const char* key,bool& flag,bool required=false) {
    const auto found=fields.find(key);
    if(found==fields.end())return !required;
    flag=found->second=="1";
    return flag || found->second=="0";
}
inline bool ParseClientPlayback(const std::string& input,ClientPlaybackRecord& out,std::string& error) {
    const auto fail=[&](const char* reason){error=reason;return false;};
    if(input.size()>16384)return fail("client-payload-too-large");
    if(input.find('\0')!=std::string::npos)return fail("client-invalid-nul");
    std::map<std::string,std::string> timeline;
    std::string metadata;
    for(size_t p=0;p<input.size();) {
        auto end=input.find('&',p);if(end==std::string::npos)end=input.size();
        const auto eq=input.find('=',p);if(eq==std::string::npos||eq>=end)return fail("client-invalid-pair");
        const auto key=input.substr(p,eq-p),value=input.substr(eq+1,end-eq-1);
        if(key=="position"||key=="sampled_ms"||key=="paused"||key=="buffering"||key=="local"||key=="instance"||key=="speed"||key=="controlled"||key=="prior_control") {
            if(!timeline.emplace(key,value).second)return fail("client-duplicate-field");
        } else {
            if(!metadata.empty())metadata+='&';
            metadata.append(input,p,end-p);
        }
        p=end+1;
    }
    ClientPlaybackRecord record;
    if(!ParseRichMetadata(metadata,record.metadata,error))return false;
    if(!ValidSpotifyMediaUri(record.metadata.uri))return fail("client-invalid-uri");
    if(!ClientNumber(timeline["position"],record.position)||record.position<0||record.position>record.metadata.duration)return fail("client-invalid-position");
    if(!ClientNumber(timeline["sampled_ms"],record.sampled_ms)||record.sampled_ms<=0)return fail("client-invalid-clock");
    bool paused=false;
    if(!ClientFlag(timeline,"paused",paused,true)||!ClientFlag(timeline,"buffering",record.buffering,true))return fail("client-invalid-playback-flags");
    record.playing=!paused;
    if(!ClientFlag(timeline,"controlled",record.explicit_control))return fail("client-invalid-control-flag");
    if(!ClientFlag(timeline,"prior_control",record.prior_control))return fail("client-invalid-prior-control-flag");
    if(timeline.count("speed")) {
        const auto& speed=timeline["speed"];
        if(speed=="1")record.speed=1;else if(speed=="2")record.speed=2;
        else if(speed=="4")record.speed=4;else if(speed=="8")record.speed=8;
        else if(speed=="10")record.speed=10;else return fail("client-invalid-speed");
    }
    if(timeline["local"]=="1")record.local=1;
    else if(timeline["local"]=="0")record.local=0;
    else if(timeline["local"]!="unknown")return fail("client-invalid-device-flag");
    if(!ClientInstance(timeline["instance"],record.instance))return fail("client-invalid-instance");
    out=std::move(record);error.clear();return true;
}
struct ClientTransition {std::string uri,instance;double sampled_ms=0,transition_ms=0,position=-1;bool explicit_skip=false,explicit_control=false,control_monitoring=false,natural=false,pending=false,terminal=false;};
inline bool ParseClientTransition(const std::string& input,ClientTransition& out) {
    if(input.size()>16384||input.find('\0')!=std::string::npos)return false;
    std::map<std::string,std::string> values;
    for(size_t p=0;p<input.size();) {
        auto end=input.find('&',p);if(end==std::string::npos)end=input.size();
        const auto eq=input.find('=',p);if(eq==std::string::npos||eq>=end)return false;
        const auto key=input.substr(p,eq-p);
        if(key!="v"&&key!="blocked"&&key!="uri"&&key!="instance"&&key!="sampled_ms"&&key!="transition_ms"&&key!="position_ms"&&key!="manual"&&key!="controlled"&&key!="monitoring"&&key!="natural"&&key!="pending"&&key!="terminal")return false;
        if(!values.emplace(key,input.substr(eq+1,end-eq-1)).second)return false;
        p=end+1;
    }
    if(values["v"]!="1"||values["blocked"]!="buffering")return false;
    ClientTransition record;
    // Preserve the aggregate encoded identity/clock budget of the original wire parser.
    if(values["uri"].size()+values["instance"].size()+values["sampled_ms"].size()>16384-133)return false;
    if(!DecodeMetadataText(values["uri"],record.uri) || !ValidSpotifyMediaUri(record.uri) ||
       !ClientInstance(values["instance"],record.instance) ||
       !ClientNumber(values["sampled_ms"],record.sampled_ms) || record.sampled_ms<=0 ||
       !ClientNumber(values["transition_ms"],record.transition_ms) || record.transition_ms<=0 ||
       record.transition_ms>record.sampled_ms+250)return false;
    if(values.count("position_ms")) {
        if(!ClientNumber(values["position_ms"],record.position))return false;
        record.position/=1000;
        if(record.position<0||record.position>86400)return false;
    }
    if(!ClientFlag(values,"manual",record.explicit_skip) || !ClientFlag(values,"controlled",record.explicit_control) ||
       !ClientFlag(values,"monitoring",record.control_monitoring,true) || !ClientFlag(values,"natural",record.natural) ||
       !ClientFlag(values,"pending",record.pending) || !ClientFlag(values,"terminal",record.terminal))return false;
    if(record.natural && record.pending)return false;
    if(record.terminal && (record.uri.rfind("spotify:episode:",0)!=0 || record.instance.empty() ||
       !record.control_monitoring || record.natural || record.pending || record.explicit_skip || record.explicit_control))return false;
    out=std::move(record);return true;
}

}
