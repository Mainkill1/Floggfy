#pragma once
#include "library_layout.h"
#include <array>
#include <string_view>
namespace history {
struct RecordingFormat {
    Codec codec;
    const wchar_t* extension;
    std::string_view capture;
};
inline constexpr std::array<RecordingFormat,4> kRecordingFormats{{
    {Codec::Flac,L".flac","native compressed FLAC input"},
    {Codec::Vorbis,L".ogg","native compressed Ogg pages"},
    {Codec::Aac,L".m4a","native compressed SpAC AAC packets"},
    {Codec::Mp3,L".mp3","native compressed MP3 input"},
}};

inline bool MatchesRecordingCapture(Codec codec,std::string_view capture) {
    for(const auto& format:kRecordingFormats)
        if(format.codec==codec)return format.capture==capture;
    return false;
}
}
