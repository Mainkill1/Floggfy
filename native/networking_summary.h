#pragma once
#include <initializer_list>
#include <string>
#include <string_view>

namespace history {
// Renderer IPC is untrusted. Accept exact schemas and reconstruct only bounded
// numeric fields / fixed enums. Never forward a URL, identifier, body or error text.
namespace networking_summary {
struct Field {std::string_view key,values;};
inline bool Number(std::string_view value,bool signed_value=false) {
    if(value=="?")return true;
    if(signed_value&&!value.empty()&&value.front()=='-')value.remove_prefix(1);
    if(value.empty()||value.size()>10)return false;
    unsigned long long n=0;
    for(char c:value) {
        if(c<'0'||c>'9')return false;
        n=n*10+unsigned(c-'0');
    }
    return n<=(signed_value?99999ULL:4294967295ULL);
}
inline bool Choice(std::string_view value,std::string_view choices) {
    while(!choices.empty()) {
        const auto end=choices.find('|');
        if(value==choices.substr(0,end))return true;
        if(end==std::string_view::npos)break;
        choices.remove_prefix(end+1);
    }
    return false;
}
inline bool Fields(std::string_view text,std::initializer_list<Field> schema) {
    for(const auto& field:schema) {
        if(text.substr(0,field.key.size())!=field.key)return false;
        text.remove_prefix(field.key.size());
        const auto end=text.find(' ');
        const auto value=text.substr(0,end);
        const bool valid=field.values=="number"?Number(value):
            field.values=="signed"?Number(value,true):Choice(value,field.values);
        if(!valid)return false;
        if(end==std::string_view::npos)text={};else text.remove_prefix(end+1);
    }
    return text.empty();
}
inline bool Prefix(std::string_view& text,std::string_view prefix) {
    if(text.substr(0,prefix.size())!=prefix)return false;
    text.remove_prefix(prefix.size());return true;
}
}
inline bool IsRendererDiagnostic(std::string_view text) noexcept {
    return text.substr(0,15)=="FLOGGFY_STATUS:"||text.substr(0,19)=="FLOGGFY_NETWORK_V1:";
}
inline bool IsRendererDiagnostic(std::wstring_view text) noexcept {
    return text.substr(0,15)==L"FLOGGFY_STATUS:"||text.substr(0,19)==L"FLOGGFY_NETWORK_V1:";
}
inline bool SummarizeClientNetworking(std::string_view text,std::string& summary) noexcept {
    using namespace networking_summary;
    summary.clear();
    if(text.size()>200)return false;
    try {
        const auto original=text;
        if(Prefix(text,"FLOGGFY_NETWORK_V1:request ")) {
            if(!Fields(text,{{"op=","number"},{"kind=","metadata|album|lyrics"},
                {"phase=","send|response|rejected|expired|setup_error"},{"code=","signed"},{"elapsed_ms=","number"}}))return false;
            const bool outbound=text.find(" phase=send ")!=std::string_view::npos;
            const bool incoming=text.find(" phase=response ")!=std::string_view::npos;
            summary=std::string("direction=")+(outbound?"out":incoming?"in":"local")+" source=enrichment "+std::string(text);
            const auto start=summary.find("kind=");summary.replace(start,5,"service=");return true;
        }
        if(Prefix(text,"FLOGGFY_STATUS:connect ")) {
            if(!Fields(text,{{"state=","not_connected|connecting|connected|no_devices|unknown"},
                {"target=","local|remote|unknown"},{"device_changed=","0|1|?"},{"controller=","unknown"}}))return false;
            summary="direction=local source=connect-cache "+std::string(text);return true;
        }
        text=original;
        if(Prefix(text,"FLOGGFY_STATUS:control ")) {
            if(!(Fields(text,{{"request=","play|pause|resume|skipToNext|skipToPrevious"},{"source=","local-client"}})||
                 Fields(text,{{"state=","paused|playing"},{"target=","local|remote|unknown"},{"initiator=","unknown"}})||
                 Fields(text,{{"observations_omitted=","number"}})))return false;
            summary="direction=local source=client-control "+std::string(text);return true;
        }
        text=original;
        if(Prefix(text,"FLOGGFY_STATUS:client obs ")) {
            if(!Fields(text,{{"paused=","0|1|?"},{"buf=","0|1|?"},{"active=","0|1|?"},
                {"code=","signed"},{"status=","signed"},{"inst=","0|1|?"},{"dev=","0|1|?"}}))return false;
            summary="direction=local source=client-state "+std::string(text);return true;
        }
    }catch(...) {summary.clear();}
    return false;
}
enum class RendererDiagnosticKind {Invalid,Status,Network};
inline RendererDiagnosticKind SanitizeRendererDiagnostic(std::string_view text,std::string& summary) noexcept {
    summary.clear();
    const bool request=text.substr(0,19)=="FLOGGFY_NETWORK_V1:";
    if(!IsRendererDiagnostic(text)||text.size()>(request?159u:99u))return RendererDiagnosticKind::Invalid;
    try {
        if(SummarizeClientNetworking(text,summary))return RendererDiagnosticKind::Network;
    }catch(...) {summary.clear();}
    return RendererDiagnosticKind::Invalid;
}
}
