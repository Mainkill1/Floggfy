#include "../native/download_policy.h"
#include <cassert>
#include <cstdio>
using namespace history;
int main() {
    struct PolicySettings {bool downloads,ogg,flac,aac,podcasts;};
    PolicySettings settings{};
    settings.downloads=true;settings.ogg=true;settings.flac=true;settings.aac=false;settings.podcasts=true;
    assert(SourceFormatEnabled(Codec::Vorbis,settings));
    assert(SourceFormatEnabled(Codec::Flac,settings));
    assert(!SourceFormatEnabled(Codec::Aac,settings));
    assert(SourceFormatEnabled(Codec::Mp3,settings));
    settings.podcasts=false;
    assert(SourceFormatEnabled(Codec::Mp3,settings));
    assert(MediaTypeEnabled(MediaKind::Music,settings));
    assert(!MediaTypeEnabled(MediaKind::Podcast,settings));
    assert(PublicationEnabled(Codec::Vorbis,MediaKind::Music,settings));
    assert(!PublicationEnabled(Codec::Vorbis,MediaKind::Podcast,settings));
    settings.podcasts=true;
    assert(PublicationEnabled(Codec::Vorbis,MediaKind::Podcast,settings));
    assert(PublicationEnabled(Codec::Flac,MediaKind::Podcast,settings));
    assert(!PublicationEnabled(Codec::Aac,MediaKind::Podcast,settings));
    assert(PublicationEnabled(Codec::Mp3,MediaKind::Podcast,settings));
    settings.downloads=false;
    assert(!PublicationEnabled(Codec::Vorbis,MediaKind::Music,settings));
    assert(!PublicationEnabled(Codec::Mp3,MediaKind::Podcast,settings));
    std::puts("PASS: source codec and media-type publication policy are independent");
}
