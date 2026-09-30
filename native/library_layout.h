#pragma once
#include <cstdint>
#include <string>
namespace history {
enum class MediaKind { Music, Compilation, Soundtrack, VariousArtists, Audiobook, Podcast, MusicVideo, Concert };
struct Catalog {
    MediaKind kind=MediaKind::Music;
    std::wstring artist,album,title,author,series,book,show;
    unsigned track=0,episode_year=0;
};
std::wstring RelativePath(const Catalog& item,const std::wstring& extension);
std::wstring OutputPath(const std::wstring& root,const Catalog& item,const std::wstring& extension,bool music_folder=false);
enum class Codec { Vorbis, Flac };
struct Quality { Codec codec=Codec::Vorbis; unsigned rate=0,channels=0,bits=0,bitrate=0; };
bool HigherQuality(const Quality& incoming,const Quality& existing);
}
