#include "library_layout.h"
#include <cwctype>
#include <cstdio>
#include <vector>
namespace history {
static std::wstring Safe(std::wstring s,const wchar_t* fallback) {
    for(wchar_t& c:s) if(c<32 || std::wstring(L"<>:\"/\\|?*").find(c)!=std::wstring::npos) c=L'-';
    while(!s.empty() && (s.back()==L'.' || s.back()==L' ')) s.pop_back();
    if(s.empty()) s=fallback;
    if(s.size()>100) s.resize(100);
    std::wstring base=s.substr(0,s.find(L'.'));
    for(wchar_t& c:base) c=wchar_t(towupper(c));
    if(base==L"CON" || base==L"PRN" || base==L"AUX" || base==L"NUL" ||
       (base.size()==4 && (base.substr(0,3)==L"COM" || base.substr(0,3)==L"LPT") && base[3]>=L'1' && base[3]<=L'9')) s=L"_"+s;
    return s;
}
std::wstring RelativePath(const Catalog& c,const std::wstring& extension) {
    std::vector<std::wstring> parts;
    auto album=Safe(c.album,L"Unknown Album");
    switch(c.kind) {
        case MediaKind::Music: parts={L"Music",L"Artists",Safe(c.artist,L"Unknown Artist"),album}; break;
        case MediaKind::Compilation: parts={L"Music",L"Compilations",album}; break;
        case MediaKind::Soundtrack: parts={L"Music",L"Soundtracks",album}; break;
        case MediaKind::VariousArtists: parts={L"Music",L"Various Artists",album}; break;
        case MediaKind::Audiobook:
            parts={L"Audiobooks",Safe(c.author.empty()?c.artist:c.author,L"Unknown Author")};
            if(!c.series.empty()) parts.push_back(Safe(c.series,L"Series"));
            parts.push_back(Safe(c.book.empty()?c.album:c.book,L"Unknown Book")); break;
        case MediaKind::Podcast:
            parts={L"Podcasts",Safe(c.show.empty()?c.album:c.show,L"Unknown Podcast")};
            if(c.episode_year) parts.push_back(std::to_wstring(c.episode_year));
            break;
        case MediaKind::MusicVideo: parts={L"Music Videos"}; break;
        case MediaKind::Concert: parts={L"Concerts"}; break;
    }
    std::wstring out;
    for(const auto& part:parts) { out+=part; out+=L'\\'; }
    if(c.track) { wchar_t number[20]; swprintf(number,20,L"%02u - ",c.track); out+=number; }
    return out+Safe(c.title,L"Untitled")+extension;
}
std::wstring OutputPath(const std::wstring& root,const Catalog& item,const std::wstring& extension,bool music_folder) {
    auto relative=RelativePath(item,extension);
    if(music_folder && relative.rfind(L"Music\\",0)==0) relative.erase(0,6);
    auto base=root;
    while(!base.empty() && (base.back()==L'\\' || base.back()==L'/')) base.pop_back();
    return base+L"\\"+relative;
}
bool HigherQuality(const Quality& incoming,const Quality& existing) {
    if(!incoming.rate || !incoming.channels) return false;
    if(incoming.codec!=existing.codec) return incoming.codec==Codec::Flac && incoming.bits>=16 && incoming.channels==existing.channels && incoming.rate>=existing.rate;
    if(incoming.channels!=existing.channels || !existing.rate) return false;
    if(incoming.codec==Codec::Vorbis) return incoming.bitrate && existing.bitrate &&
        incoming.bitrate>existing.bitrate && incoming.rate>=existing.rate;
    return incoming.bits && existing.bits && incoming.bits>=existing.bits && incoming.rate>=existing.rate &&
        (incoming.bits>existing.bits || incoming.rate>existing.rate);
}
}
