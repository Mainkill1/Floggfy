#include "library_layout.h"
#include <cwctype>
#include <cstdio>
#include <climits>
#include <cstdint>
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
std::wstring RelativePath(const Catalog& c,const std::wstring& extension,bool track_numbers) {
    std::vector<std::wstring> parts;
    auto album=Safe(c.album,L"Unknown Album");
    const auto& folder_artist=c.folder_artist.empty()?c.artist:c.folder_artist;
    switch(c.kind) {
        case MediaKind::Music: parts={L"Music",L"Artists",Safe(folder_artist,L"Unknown Artist"),album}; break;
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
    if(track_numbers && c.track) {
        wchar_t number[20];swprintf(number,20,L"%02u - ",c.track);out+=number;
    }
    return out+Safe(c.title,L"Untitled")+extension;
}
std::wstring OutputPath(const std::wstring& root,const Catalog& item,const std::wstring& extension,bool music_folder,bool track_numbers) {
    auto relative=RelativePath(item,extension,track_numbers);
    if(music_folder && relative.rfind(L"Music\\",0)==0) relative.erase(0,6);
    auto base=root;
    while(!base.empty() && (base.back()==L'\\' || base.back()==L'/')) base.pop_back();
    return base+L"\\"+relative;
}
bool ValidSpotifyTrackUri(const std::string& uri) noexcept {
    constexpr char prefix[]="spotify:track:";constexpr size_t prefix_size=sizeof(prefix)-1,id_size=22;
    if(uri.size()!=prefix_size+id_size || uri.compare(0,prefix_size,prefix))return false;
    for(size_t i=prefix_size;i<uri.size();++i)if(!((uri[i]>='0'&&uri[i]<='9')||
       (uri[i]>='A'&&uri[i]<='Z')||(uri[i]>='a'&&uri[i]<='z')))return false;
    return true;
}
static std::wstring MissingTrackIdSuffix(const Catalog& item) {
    const auto identity=item.artist+L'\0'+item.album+L'\0'+item.title+L'\0'+
        std::to_wstring(item.disc)+L":"+std::to_wstring(item.track)+L":"+std::to_wstring(item.duration);
    uint64_t hash=14695981039346656037ULL;
    auto unit=[&](uint32_t value) {
        for(unsigned shift:{0u,8u}) {hash^=(value>>shift)&255;hash*=1099511628211ULL;}
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
static std::wstring DuplicateOutputPathWithSuffix(const std::wstring& root,const Catalog& item,const std::wstring& extension,
                                               bool music_folder,bool track_numbers,const std::wstring& suffix) {
    auto relative=RelativePath(item,extension,track_numbers);
    if(music_folder && relative.rfind(L"Music\\",0)==0)relative.erase(0,6);
    const auto slash=relative.find_last_of(L'\\');
    if(slash==std::wstring::npos)return {};
    std::wstring prefix;
    if(track_numbers&&item.track){wchar_t number[20];std::swprintf(number,20,L"%02u - ",item.track);prefix=number;}
    auto title=Safe(item.title,L"Untitled");
    constexpr size_t component_limit=100;
    if(prefix.size()+suffix.size()>=component_limit)return {};
    const auto available=component_limit-prefix.size()-suffix.size();
    if(title.size()>available)title.resize(available);
#if WCHAR_MAX <= 0xffff
    if(!title.empty()&&title.back()>=0xd800&&title.back()<=0xdbff)title.pop_back();
#endif
    while(!title.empty()&&(title.back()==L'.'||title.back()==L' '))title.pop_back();
    if(title.empty())title=L"Untitled";
    relative=relative.substr(0,slash+1)+prefix+title+suffix+extension;
    auto base=root;while(!base.empty()&&(base.back()==L'\\'||base.back()==L'/'))base.pop_back();
    return base+L"\\Duplicates\\"+relative;
}
std::vector<std::wstring> DuplicateOutputPaths(const std::wstring& root,const Catalog& item,const std::wstring& extension,bool music_folder,bool track_numbers) {
    if(!ValidSpotifyTrackUri(item.spotify_uri)) {
        auto path=DuplicateOutputPathWithSuffix(root,item,extension,music_folder,track_numbers,MissingTrackIdSuffix(item));
        return path.empty()?std::vector<std::wstring>{}:std::vector<std::wstring>{std::move(path)};
    }
    std::vector<std::wstring> paths;paths.reserve(3);
    const auto id=item.spotify_uri.substr(sizeof("spotify:track:")-1);
    for(const size_t characters:{size_t(10),size_t(14),size_t(22)}) {
        std::wstring suffix=L" [t-";suffix.append(id.end()-characters,id.end());suffix+=L']';
        auto path=DuplicateOutputPathWithSuffix(root,item,extension,music_folder,track_numbers,suffix);
        if(path.empty())return {};
        paths.push_back(std::move(path));
    }
    return paths;
}
std::wstring DuplicateOutputPath(const std::wstring& root,const Catalog& item,const std::wstring& extension,bool music_folder,bool track_numbers) {
    auto paths=DuplicateOutputPaths(root,item,extension,music_folder,track_numbers);
    return paths.empty()?std::wstring{}:std::move(paths.front());
}
std::vector<std::wstring> DuplicateLookupPaths(const std::wstring& root,const Catalog& item,const std::wstring& extension,bool music_folder,bool track_numbers) {
    auto paths=DuplicateOutputPaths(root,item,extension,music_folder,track_numbers);
    if(!paths.empty()&&ValidSpotifyTrackUri(item.spotify_uri)) {
        auto path=DuplicateOutputPathWithSuffix(root,item,extension,music_folder,track_numbers,MissingTrackIdSuffix(item));
        if(!path.empty())paths.push_back(std::move(path));
    }
    return paths;
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
