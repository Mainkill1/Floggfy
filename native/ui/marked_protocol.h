#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace history {
// Renderer messages carry only a request token and exact Spotify identities.
// Delimiters cannot occur in either field; no JSON parser or paths are needed.
inline bool ParseMarkedQuery(std::wstring_view message,std::string& request,std::vector<std::string>& uris) {
    constexpr std::wstring_view prefix=L"FLOGGFY_MARKED_V1:";
    request.clear();uris.clear();
    if(message.size()>12000||message.substr(0,prefix.size())!=prefix)return false;
    message.remove_prefix(prefix.size());
    auto end=message.find(L'|');
    if(end==message.npos||end==0||end>64)return false;
    const auto alpha=[](wchar_t c){return (c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9');};
    for(auto c:message.substr(0,end)) {
        if(!alpha(c)&&c!=L'_'&&c!=L'-')return false;
        request+=char(c);
    }
    message.remove_prefix(end+1);
    while(!message.empty()) {
        end=message.find(L'|');
        const auto uri=message.substr(0,end);
        if(uris.size()>=256||uri.size()!=36||uri.substr(0,14)!=L"spotify:track:")return false;
        std::string value;value.reserve(36);
        for(size_t i=0;i<uri.size();++i){if(i>=14&&!alpha(uri[i]))return false;value+=char(uri[i]);}
        uris.push_back(std::move(value));
        if(end==message.npos)break;
        message.remove_prefix(end+1);
        if(message.empty())return false;
    }
    return !uris.empty();
}
}
