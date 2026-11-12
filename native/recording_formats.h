#pragma once
#include "library_layout.h"
#include <array>
namespace history {
struct RecordingFormat {
    Codec codec;
    const wchar_t* extension;
};
inline constexpr std::array<RecordingFormat,4> kRecordingFormats{{
    {Codec::Flac,L".flac"},
    {Codec::Vorbis,L".ogg"},
    {Codec::Aac,L".m4a"},
    {Codec::Mp3,L".mp3"},
}};
}
