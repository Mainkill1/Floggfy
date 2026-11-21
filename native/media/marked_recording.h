#pragma once
#include "spotify_uri.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace history {
struct MarkedRecording {
    std::string uri,saved_utc;
    uint64_t bytes=0,write_time=0;
    bool complete=false,owned=false;
};

inline bool ValidMarkedDate(const std::string& date) {
    if(date.size()!=20 || date[4]!='-' || date[7]!='-' || date[10]!='T' ||
       date[13]!=':' || date[16]!=':' || date[19]!='Z')return false;
    for(unsigned at:{0u,1u,2u,3u,5u,6u,8u,9u,11u,12u,14u,15u,17u,18u})
        if(date[at]<'0' || date[at]>'9')return false;
    const auto pair=[&](unsigned at){return unsigned((date[at]-'0')*10+date[at+1]-'0');};
    const auto year=pair(0)*100+pair(2),month=pair(5),day=pair(8);
    const unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap=year%4==0 && (year%100!=0 || year%400==0);
    return year && month && month<=12 && day && day<=days[month-1]+(month==2 && leap) &&
           pair(11)<24 && pair(14)<60 && pair(17)<60;
}

// A successful header read reports tag claims only. It does not validate audio.
using MarkedReadAt=std::function<bool(uint64_t,void*,size_t)>;
bool ReadMarkedRecording(uint64_t bytes,const MarkedReadAt& read,MarkedRecording& out,std::string& error);
bool ReadMarkedRecording(const std::wstring& path,MarkedRecording& out,std::string& error);
bool BackfillMarkedDate(const std::wstring& path,unsigned expected_generation);
}
