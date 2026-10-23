// Exercises production enrichment with an offline artwork-cache boundary.
#include "../native/metadata_bridge.cpp"
#include "../native/album_artwork.h"
#include "artwork_fixture.h"
#include <cassert>
namespace {
std::string ready_uri;
std::vector<uint8_t> ready_cover;
std::string ready_mime;
unsigned artwork_reads=0;
}
namespace history {
void CacheAlbumArtwork(const std::string&,const std::string&) {}
bool GetAlbumArtwork(const std::string& uri,std::vector<uint8_t>& bytes,std::string& mime) {
 ++artwork_reads;
 if(uri!=ready_uri || ready_cover.empty())return false;
 bytes=ready_cover;mime=ready_mime;return true;
}
}
int main() {
 history::RichMetadata record;record.title="Song";record.artist="Artist";record.album="Album";
 record.uri="spotify:track:0000000000000000000001";record.duration=200;
 history::cache.Put(record);
 history::Media media;media.title=L"Song";media.artist=L"Artist";media.album=L"Album";media.duration=200;
 ready_uri=record.uri;ready_cover=artwork_fixture::jpeg();ready_mime="image/jpeg";
 history::Tags tags;tags.cover=artwork_fixture::png();tags.mime="image/png";
 history::EnrichTags(media,tags);
 assert(tags.cover==ready_cover && tags.mime==ready_mime && "larger original cover must replace thumbnail");
 ready_cover=artwork_fixture::png();ready_mime="image/png";
 history::EnrichTags(media,tags);
 assert(tags.cover==ready_cover && tags.mime=="image/png" && "validated original must replace a larger possibly branded thumbnail");
 const auto original=tags.cover;
 ready_cover={1,2,3};
 history::EnrichTags(media,tags);
 assert(tags.cover==original && tags.mime=="image/png");
 ready_cover=artwork_fixture::jpeg();ready_mime="image/jpeg";tags.cover.clear();tags.mime.clear();
 history::EnrichTags(media,tags);assert(tags.cover==ready_cover);
 const auto reads=artwork_reads;media.artist=L"Other";tags.cover=artwork_fixture::png();
 const auto fallback=tags.cover;history::EnrichTags(media,tags);
 assert(tags.cover==fallback && artwork_reads==reads && "unmatched track must not receive another track's cover");
 puts("PASS: exact-track original artwork enrichment, original-source preference and safe fallback");
}
