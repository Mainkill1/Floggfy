#pragma once
#include "library_layout.h"
namespace history {
template<class SettingsLike>
inline bool SourceFormatEnabled(Codec codec,const SettingsLike& settings) noexcept {
    switch(codec) {
        case Codec::Vorbis:return settings.ogg;
        case Codec::Flac:return settings.flac;
        case Codec::Aac:return settings.aac;
        case Codec::Mp3:return true;
    }
    return false;
}
template<class SettingsLike>
inline bool MediaTypeEnabled(MediaKind kind,const SettingsLike& settings) noexcept {
    return kind!=MediaKind::Podcast||settings.podcasts;
}
template<class SettingsLike>
inline bool PublicationEnabled(Codec codec,MediaKind kind,const SettingsLike& settings) noexcept {
    return settings.downloads&&SourceFormatEnabled(codec,settings)&&MediaTypeEnabled(kind,settings);
}
}
