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
    int local=-1;
    std::string instance;
};
inline bool ParseClientPlayback(const std::string& input,ClientPlaybackRecord& out,std::string& error) {
    const auto fail=[&](const char* reason){error=reason;return false;};
    if(input.size()>16384)return fail("client-payload-too-large");
    std::map<std::string,std::string> timeline;
    std::string metadata;
    for(size_t p=0;p<input.size();) {
        auto end=input.find('&',p);if(end==std::string::npos)end=input.size();
        const auto eq=input.find('=',p);if(eq==std::string::npos||eq>=end)return fail("client-invalid-pair");
        const auto key=input.substr(p,eq-p),value=input.substr(eq+1,end-eq-1);
        if(key=="position"||key=="sampled_ms"||key=="paused"||key=="buffering"||key=="local"||key=="instance") {
            if(!timeline.emplace(key,value).second)return fail("client-duplicate-field");
        } else {
            if(!metadata.empty())metadata+='&';
            metadata.append(input,p,end-p);
        }
        p=end+1;
    }
    ClientPlaybackRecord record;
    if(!ParsePlaybackMetadata(metadata,record.metadata,error))return false;
    const auto& uri=record.metadata.uri;
    const size_t prefix=uri.rfind("spotify:track:",0)==0?14:uri.rfind("spotify:episode:",0)==0?16:0;
    if(!prefix||uri.size()!=prefix+22||!std::all_of(uri.begin()+prefix,uri.end(),[](unsigned char c){
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');}))return fail("client-invalid-uri");
    const auto number=[&](const char* key,double& result){
        const auto found=timeline.find(key);if(found==timeline.end()||found->second.empty())return false;
        char* last=nullptr;result=std::strtod(found->second.c_str(),&last);
        return last&&!*last&&std::isfinite(result);
    };
    if(!number("position",record.position)||record.position<0||record.position>record.metadata.duration)return fail("client-invalid-position");
    if(!number("sampled_ms",record.sampled_ms)||record.sampled_ms<=0)return fail("client-invalid-clock");
    if((timeline["paused"]!="0"&&timeline["paused"]!="1")||
       (timeline["buffering"]!="0"&&timeline["buffering"]!="1"))return fail("client-invalid-playback-flags");
    record.playing=timeline["paused"]=="0";record.buffering=timeline["buffering"]=="1";
    if(timeline["local"]=="1")record.local=1;
    else if(timeline["local"]=="0")record.local=0;
    else if(timeline["local"]!="unknown")return fail("client-invalid-device-flag");
    if(timeline.count("instance")) {
        const auto& encoded=timeline["instance"];
        for(size_t i=0;i<encoded.size();++i) {
            unsigned char c=encoded[i];
            if(c=='%') {
                const auto hex=[](char v){return v>='0'&&v<='9'?v-'0':v>='a'&&v<='f'?v-'a'+10:v>='A'&&v<='F'?v-'A'+10:-1;};
                if(i+2>=encoded.size()||hex(encoded[i+1])<0||hex(encoded[i+2])<0)return fail("client-invalid-instance");
                c=unsigned(hex(encoded[i+1])*16+hex(encoded[i+2]));i+=2;
            }
            if(c<=32||c>=127||record.instance.size()>=128)return fail("client-invalid-instance");
            record.instance+=char(c);
        }
    }
    out=std::move(record);error.clear();return true;
}
struct ClientTransition {std::string uri,instance;double sampled_ms=0,transition_ms=0,position=-1;};
inline bool ParseClientTransition(const std::string& input,ClientTransition& out) {
    std::map<std::string,std::string> values;
    for(size_t p=0;p<input.size();) {
        auto end=input.find('&',p);if(end==std::string::npos)end=input.size();
        const auto eq=input.find('=',p);if(eq==std::string::npos||eq>=end)return false;
        const auto key=input.substr(p,eq-p);
        if(key!="v"&&key!="blocked"&&key!="uri"&&key!="instance"&&key!="sampled_ms"&&key!="transition_ms"&&key!="position_ms")return false;
        if(!values.emplace(key,input.substr(eq+1,end-eq-1)).second)return false;
        p=end+1;
    }
    if(values["v"]!="1"||values["blocked"]!="buffering")return false;
    const auto protocol=std::string("v=1&title=transition&artist=transition&album=transition&duration=1&position=0&paused=0&buffering=1&local=1&uri=")+values["uri"]+
        "&instance="+values["instance"]+"&sampled_ms="+values["sampled_ms"];
    ClientPlaybackRecord record;std::string error;
    if(!ParseClientPlayback(protocol,record,error)||values["transition_ms"].empty())return false;
    char* end=nullptr;const double changed=std::strtod(values["transition_ms"].c_str(),&end);
    if(!end||*end||!std::isfinite(changed)||changed<=0||changed>record.sampled_ms+250)return false;
    double position=-1;
    if(values.count("position_ms")) {
        if(values["position_ms"].empty())return false;
        char* position_end=nullptr;position=std::strtod(values["position_ms"].c_str(),&position_end)/1000;
        if(!position_end||*position_end||!std::isfinite(position)||position<0||position>86400)return false;
    }
    out={record.metadata.uri,record.instance,record.sampled_ms,changed,position};return true;
}

}
