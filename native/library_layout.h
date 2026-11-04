#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace history {
enum class MediaKind { Music, Compilation, Soundtrack, VariousArtists, Audiobook, Podcast, MusicVideo, Concert };
struct Catalog {
    MediaKind kind=MediaKind::Music;
    std::wstring artist,album,title,author,series,book,show;
    // A known media-session album artist anchors the existing folder even if
    // Spotify later supplies a richer artist list for the recording's tags.
    std::wstring folder_artist;
    // Lookup-only compatibility for older folders named from expanded credits.
    // Never used for new filenames or ALBUMARTIST tags.
    std::wstring legacy_artist;
    unsigned track=0,episode_year=0,disc=0;
    std::string spotify_uri,spotify_album_uri;
    double duration=0;
};
std::wstring RelativePath(const Catalog& item,const std::wstring& extension,bool track_numbers=false);
std::wstring OutputPath(const std::wstring& root,const Catalog& item,const std::wstring& extension,bool music_folder=false,bool track_numbers=false);
bool ValidSpotifyTrackUri(const std::string& uri) noexcept;
// Additive fallbacks for an explicit different-track UID collision. Names use
// the shortest display suffix first; full URI metadata remains authoritative.
std::vector<std::wstring> DuplicateOutputPaths(const std::wstring& root,const Catalog& item,const std::wstring& extension,bool music_folder=false,bool track_numbers=false);
// Compatibility shorthand for the first (10-character) duplicate path.
std::wstring DuplicateOutputPath(const std::wstring& root,const Catalog& item,const std::wstring& extension,bool music_folder=false,bool track_numbers=false);
enum class Codec { Vorbis, Flac, Aac, Mp3 };
struct Quality { Codec codec=Codec::Vorbis; unsigned rate=0,channels=0,bits=0,bitrate=0; };
bool HigherQuality(const Quality& incoming,const Quality& existing);
}
