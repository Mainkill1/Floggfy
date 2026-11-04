#include "../native/library_layout.h"
#include <cstdio>
#include <cstdlib>
using namespace history;
static void check(bool b,const char* n) { if(!b) { std::fprintf(stderr,"FAIL: %s\n",n); std::exit(1); } }
static bool ends(const std::wstring& value,const std::wstring& suffix) {
    return value.size()>=suffix.size()&&value.compare(value.size()-suffix.size(),suffix.size(),suffix)==0;
}
#ifdef FLOGGFY_SONG_SHORTCUT_TEST
int RunLibraryLayoutTests() {
#else
int main() {
#endif
    Catalog c; c.artist=L"Test/Artist"; c.album=L"Test Album"; c.title=L"Test Track"; c.track=1;
    check(RelativePath(c,L".ogg")==L"Music\\Artists\\Test-Artist\\Test Album\\Test Track.ogg",
          "artist album layout without year or filename timestamp");
    check(OutputPath(L"C:\\Users\\TestUser\\Music",c,L".ogg",true)==L"C:\\Users\\TestUser\\Music\\Artists\\Test-Artist\\Test Album\\Test Track.ogg","default Music folder avoids duplicate Music directory");
    check(OutputPath(L"D:\\Media\\",c,L".ogg")==L"D:\\Media\\Music\\Artists\\Test-Artist\\Test Album\\Test Track.ogg","custom Media root retains hierarchy");
    c.spotify_uri="spotify:track:0000000000000000000001";
    check(DuplicateOutputPath(L"C:\\Users\\TestUser\\Music",c,L".ogg",true)==
          L"C:\\Users\\TestUser\\Music\\Duplicates\\Artists\\Test-Artist\\Test Album\\Test Track [spotify-0000000000000000000001].ogg",
          "same-name UID conflict uses duplicate root and complete track identifier");
    check(DuplicateOutputPath(L"D:\\Media\\",c,L".flac")==
          L"D:\\Media\\Duplicates\\Music\\Artists\\Test-Artist\\Test Album\\Test Track [spotify-0000000000000000000001].flac",
          "custom hierarchy remains beneath duplicate root");
    const auto duplicate=DuplicateOutputPath(L"D:\\Media",c,L".ogg");
    const auto component=duplicate.substr(duplicate.find_last_of(L'\\')+1);
    check(component.size()<=104 && ends(component,L" [spotify-0000000000000000000001].ogg"),
          "duplicate filename preserves complete identifier within title and extension budget");
    auto invalid=c;invalid.spotify_uri="spotify:track:short";
    check(DuplicateOutputPath(L"D:\\Media",invalid,L".ogg").empty(),"invalid track identity cannot select duplicate namespace");
    invalid.spotify_uri.clear();
    check(DuplicateOutputPath(L"D:\\Media",invalid,L".ogg").empty(),"missing track identity cannot select duplicate namespace");
    auto long_name=c;long_name.title=std::wstring(120,L'\u97f3');
    const auto long_duplicate=DuplicateOutputPath(L"D:\\Media",long_name,L".m4a");
    const auto long_component=long_duplicate.substr(long_duplicate.find_last_of(L'\\')+1);
    check(long_component.size()<=104 && ends(long_component,L" [spotify-0000000000000000000001].m4a"),
          "long Unicode title is shortened without dropping stable identity");
    c.spotify_uri.clear();
    auto first_disc=RelativePath(c,L".flac"); c.disc=2;
    check(RelativePath(c,L".flac")==first_disc,"disc stays in metadata; identity collision handling disambiguates files"); c.disc=0;
    c.kind=MediaKind::Compilation;
    check(RelativePath(c,L".ogg")==L"Music\\Compilations\\Test Album\\Test Track.ogg","compilation routing");
    c.kind=MediaKind::Soundtrack;
    check(RelativePath(c,L".ogg").find(L"Music\\Soundtracks\\")==0,"soundtrack routing");
    c.kind=MediaKind::VariousArtists;
    check(RelativePath(c,L".ogg").find(L"Music\\Various Artists\\")==0,"various artist routing");
    c.kind=MediaKind::Audiobook; c.author=L"Author"; c.series=L"Series"; c.book=L"Book";
    check(RelativePath(c,L".flac")==L"Audiobooks\\Author\\Series\\Book\\Test Track.flac","book series routing");
    c.kind=MediaKind::Podcast; c.show=L"Podcast Name"; c.episode_year=0; c.track=0;
    check(RelativePath(c,L".mp3")==L"Podcasts\\Podcast Name\\Test Track.mp3","podcasts use a dedicated show folder");
    c.kind=MediaKind::MusicVideo;
    check(RelativePath(c,L".ogg")==L"Music Videos\\Test Track.ogg","music video routing");
    c.kind=MediaKind::Concert;
    check(RelativePath(c,L".ogg")==L"Concerts\\Test Track.ogg","concert routing");
    c.kind=MediaKind::Music; c.artist=L".."; c.album=L"CON"; c.title=L"../bad:name";
    auto path=RelativePath(c,L".ogg");
    check(path.find(L"..\\")==std::wstring::npos && path.find(L":")==std::wstring::npos && path.find(L"\\CON\\")==std::wstring::npos,
          "path traversal and reserved Windows names prevented");
    Quality high{Codec::Vorbis,44100,2,0,320000},low{Codec::Vorbis,44100,2,0,160000};
    check(HigherQuality(high,low),"higher Vorbis bitrate upgrades");
    check(!HigherQuality(low,high) && !HigherQuality(high,high),"equal or better existing Vorbis skipped");
    Quality flac{Codec::Flac,44100,2,16,0},hires{Codec::Flac,48000,2,24,0};
    check(HigherQuality(flac,high) && !HigherQuality(high,flac),"lossless existing file prevents Vorbis downgrade");
    check(HigherQuality(hires,flac) && !HigherQuality(flac,hires),"FLAC resolution upgrades");
    check(!HigherQuality(Quality{Codec::Vorbis,44100,2,0,0},high),"unknown encoding quality cannot replace file");
    std::puts("PASS: requested media hierarchy, stable names and conservative quality comparison");
    return 0;
}
