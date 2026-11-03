#pragma once
#include "library_layout.h"
#include <array>
namespace history {
struct RecordingFormat {
    Codec codec;
    const wchar_t* extension;
    bool podcasts;
};
inline constexpr std::array<RecordingFormat,4> kRecordingFormats{{
    {Codec::Flac,L".flac",false},
    {Codec::Vorbis,L".ogg",false},
    {Codec::Aac,L".m4a",false},
    {Codec::Mp3,L".mp3",true},
}};
inline constexpr bool SupportsMedia(const RecordingFormat& format,MediaKind kind) noexcept {
    return format.podcasts==(kind==MediaKind::Podcast);
}
}
