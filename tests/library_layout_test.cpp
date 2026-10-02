#include "../native/library_layout.h"
#include <cstdio>
#include <cstdlib>
using namespace history;
static void check(bool b,const char* n) { if(!b) { std::fprintf(stderr,"FAIL: %s\n",n); std::exit(1); } }
int main() {
    Catalog c; c.artist=L"Test/Artist"; c.album=L"Test Album"; c.title=L"Test Track"; c.track=1;
    check(RelativePath(c,L".ogg")==L"Music\\Artists\\Test-Artist\\Test Album\\01 - Test Track.ogg",
          "artist album layout without year or filename timestamp");
    check(OutputPath(L"C:\\Users\\TestUser\\Music",c,L".ogg",true)==L"C:\\Users\\TestUser\\Music\\Artists\\Test-Artist\\Test Album\\01 - Test Track.ogg","default Music folder avoids duplicate Music directory");
    check(OutputPath(L"D:\\Media\\",c,L".ogg")==L"D:\\Media\\Music\\Artists\\Test-Artist\\Test Album\\01 - Test Track.ogg","custom Media root retains hierarchy");
    c.kind=MediaKind::Compilation;
    check(RelativePath(c,L".ogg")==L"Music\\Compilations\\Test Album\\01 - Test Track.ogg","compilation routing");
    c.kind=MediaKind::Soundtrack;
    check(RelativePath(c,L".ogg").find(L"Music\\Soundtracks\\")==0,"soundtrack routing");
    c.kind=MediaKind::VariousArtists;
    check(RelativePath(c,L".ogg").find(L"Music\\Various Artists\\")==0,"various artist routing");
    c.kind=MediaKind::Audiobook; c.author=L"Author"; c.series=L"Series"; c.book=L"Book";
    check(RelativePath(c,L".flac")==L"Audiobooks\\Author\\Series\\Book\\01 - Test Track.flac","book series routing");
    c.kind=MediaKind::Podcast; c.show=L"Podcast Name"; c.episode_year=2026; c.track=0;
    check(RelativePath(c,L".ogg")==L"Podcasts\\Podcast Name\\2026\\Test Track.ogg","podcast year routing");
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
}
