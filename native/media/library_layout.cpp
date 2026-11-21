#include "library_layout.h"
#include "../tooling/fnv1a.h"
#include <cwctype>
#include <cstdio>
#include <climits>
#include <cstdint>
#include <string_view>
namespace history {
static std::wstring Safe(std::wstring s,const wchar_t* fallback) {
    constexpr std::wstring_view forbidden=L"<>:\"/\\|?*";
    for(wchar_t& c:s) if(c<32 || forbidden.find(c)!=std::wstring_view::npos) c=L'-';
    while(!s.empty() && (s.back()==L'.' || s.back()==L' ')) s.pop_back();
    if(s.empty()) s=fallback;
    if(s.size()>100) s.resize(100);
    std::wstring base=s.substr(0,s.find(L'.'));
    for(wchar_t& c:base) c=wchar_t(towupper(c));
    if(base==L"CON" || base==L"PRN" || base==L"AUX" || base==L"NUL" ||
       (base.size()==4 && (base.substr(0,3)==L"COM" || base.substr(0,3)==L"LPT") && base[3]>=L'1' && base[3]<=L'9')) s=L"_"+s;
    return s;
}
static std::wstring RelativeDirectory(const Catalog& c) {
    std::wstring path;
    auto component=[&](const std::wstring& value,const wchar_t* fallback) {
        path+=Safe(value,fallback);
        path+=L'\\';
    };
    const auto& folder_artist=c.folder_artist.empty()?c.artist:c.folder_artist;
    switch(c.kind) {
        case MediaKind::Music: path=L"Music\\Artists\\";component(folder_artist,L"Unknown Artist");break;
        case MediaKind::Compilation: path=L"Music\\Compilations\\";break;
        case MediaKind::Soundtrack: path=L"Music\\Soundtracks\\";break;
        case MediaKind::VariousArtists: path=L"Music\\Various Artists\\";break;
        case MediaKind::Audiobook:
            path=L"Audiobooks\\";
            component(c.author.empty()?c.artist:c.author,L"Unknown Author");
            if(!c.series.empty())component(c.series,L"Series");
            component(c.book.empty()?c.album:c.book,L"Unknown Book");
            return path;
        case MediaKind::Podcast:
            path=L"Podcasts\\";
            component(c.show.empty()?c.album:c.show,L"Unknown Podcast");
            if(c.episode_year)path+=std::to_wstring(c.episode_year)+L'\\';
            return path;
        case MediaKind::MusicVideo: return L"Music Videos\\";
        case MediaKind::Concert: return L"Concerts\\";
    }
    if(!path.empty())component(c.album,L"Unknown Album");
    return path;
}
std::wstring TrackNumberPrefix(unsigned track) {
    if(!track)return {};
    wchar_t number[20];std::swprintf(number,20,L"%02u - ",track);return number;
}
std::wstring RelativePath(const Catalog& c,const std::wstring& extension,bool track_numbers) {
    return RelativeDirectory(c)+TrackNumberPrefix(track_numbers?c.track:0)+Safe(c.title,L"Untitled")+extension;
}
static std::wstring RootedDirectory(std::wstring_view root,const Catalog& item,bool music_folder,bool duplicate) {
    while(!root.empty() && (root.back()==L'\\' || root.back()==L'/'))root.remove_suffix(1);
    const auto directory=RelativeDirectory(item);
    std::wstring_view relative=directory;
    if(music_folder && relative.substr(0,6)==L"Music\\")relative.remove_prefix(6);
    std::wstring path(root);
    path+=duplicate?L"\\Duplicates\\":L"\\";
    path+=relative;
    return path;
}
std::wstring OutputPath(const std::wstring& root,const Catalog& item,const std::wstring& extension,bool music_folder,bool track_numbers) {
    return RootedDirectory(root,item,music_folder,false)+TrackNumberPrefix(track_numbers?item.track:0)+Safe(item.title,L"Untitled")+extension;
}
static std::wstring MissingTrackIdSuffix(const Catalog& item) {
    const auto identity=item.artist+L'\0'+item.album+L'\0'+item.title+L'\0'+
        std::to_wstring(item.disc)+L":"+std::to_wstring(item.track)+L":"+std::to_wstring(item.duration);
    uint64_t hash=kFnv1aOffset;
    auto unit=[&](uint32_t value) {
        hash=Fnv1aByte(hash,uint8_t(value));
        hash=Fnv1aByte(hash,uint8_t(value>>8));
    };
    // Hash UTF-16 units identically on Windows and portable-test platforms.
    // This is a filename aid; MatchesRecording still decides identity.
    for(wchar_t ch:identity) {
        const auto value=uint32_t(ch);
        if(value>0xffff) {unit(0xd800+((value-0x10000)>>10));unit(0xdc00+((value-0x10000)&1023));}
        else unit(value);
    }
    wchar_t suffix[40];std::swprintf(suffix,40,L" [no-id-%016llx]",static_cast<unsigned long long>(hash));
    return suffix;
}
static std::vector<std::wstring> DuplicatePaths(const std::wstring& root,const Catalog& item,const std::wstring& extension,
    bool music_folder,bool track_numbers,bool include_missing_id,size_t id_lengths=3) {
    const auto directory=RootedDirectory(root,item,music_folder,true);
    const auto prefix=TrackNumberPrefix(track_numbers?item.track:0);
    const auto title=Safe(item.title,L"Untitled");
    auto with_suffix=[&](const std::wstring& suffix) {
        constexpr size_t component_limit=100;
        if(prefix.size()+suffix.size()>=component_limit)return std::wstring{};
        auto stem=std::wstring_view(title).substr(0,component_limit-prefix.size()-suffix.size());
#if WCHAR_MAX <= 0xffff
        if(!stem.empty()&&stem.back()>=0xd800&&stem.back()<=0xdbff)stem.remove_suffix(1);
#endif
        while(!stem.empty()&&(stem.back()==L'.'||stem.back()==L' '))stem.remove_suffix(1);
        if(stem.empty())stem=L"Untitled";
        auto path=directory+prefix;
        path+=stem;
        return std::move(path)+suffix+extension;
    };
    const bool valid_id=ValidSpotifyTrackUri(item.spotify_uri);
    std::vector<std::wstring> paths;
    paths.reserve(valid_id?id_lengths+include_missing_id:1);
    if(valid_id) {
        constexpr size_t lengths[]={10,14,22};
        for(size_t i=0;i<id_lengths;++i) {
            const auto end=item.spotify_uri.end();
            std::wstring suffix=L" [t-";suffix.append(end-lengths[i],end);suffix+=L']';
            auto path=with_suffix(suffix);
            if(path.empty())return {};
            paths.push_back(std::move(path));
        }
    }
    if(!valid_id||include_missing_id) {
        auto path=with_suffix(MissingTrackIdSuffix(item));
        if(!path.empty())paths.push_back(std::move(path));
    }
    return paths;
}
std::vector<std::wstring> DuplicateOutputPaths(const std::wstring& root,const Catalog& item,const std::wstring& extension,bool music_folder,bool track_numbers) {
    return DuplicatePaths(root,item,extension,music_folder,track_numbers,false);
}
std::wstring DuplicateOutputPath(const std::wstring& root,const Catalog& item,const std::wstring& extension,bool music_folder,bool track_numbers) {
    auto paths=DuplicatePaths(root,item,extension,music_folder,track_numbers,false,1);
    return paths.empty()?std::wstring{}:std::move(paths.front());
}
std::vector<std::wstring> DuplicateLookupPaths(const std::wstring& root,const Catalog& item,const std::wstring& extension,bool music_folder,bool track_numbers) {
    return DuplicatePaths(root,item,extension,music_folder,track_numbers,true);
}
bool HigherQuality(const Quality& incoming,const Quality& existing) {
    if(!incoming.rate || !incoming.channels) return false;
    if(incoming.codec!=existing.codec) return incoming.codec==Codec::Flac && incoming.bits>=16 && incoming.channels==existing.channels && incoming.rate>=existing.rate;
    if(incoming.channels!=existing.channels || !existing.rate) return false;
    // Measured AAC bitrate is not a source quality tier. Preserve an existing
    // complete AAC rendition rather than treating VBR size variation as better.
    if(incoming.codec==Codec::Aac||incoming.codec==Codec::Mp3)return false;
    if(incoming.codec==Codec::Vorbis) return incoming.bitrate && existing.bitrate &&
        incoming.bitrate>existing.bitrate && incoming.rate>=existing.rate;
    return incoming.bits && existing.bits && incoming.bits>=existing.bits && incoming.rate>=existing.rate &&
        (incoming.bits>existing.bits || incoming.rate>existing.rate);
}
}
