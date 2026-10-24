#include "../native/media_session.h"
#include <cstdio>
int main() {
    history::MediaReader reader; history::Media media;
    if (!reader.Read(media) || media.title.empty() || media.duration<=0) {
        std::fprintf(stderr,"FAIL: native Spotify media snapshot unavailable\n"); return 1;
    }
    std::printf("title=%s artist=%s duration=%.3f position=%.3f playing=%d\n",
      history::Utf8(media.title).c_str(),history::Utf8(media.artist).c_str(),
      media.duration,media.position,media.playing);
    std::puts("PASS: native media properties and timeline");
}
