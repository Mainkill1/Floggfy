#pragma once
#include <string>
#include <cstdint>
#include <map>
#include <deque>
#include <memory>
#include <vector>
namespace history {
struct MetadataArtwork {
 std::string reference,mime;
 std::vector<std::uint8_t> bytes;
};
struct RichMetadata {
 std::string title,artist,album,uri,album_uri,artwork_url;
 std::vector<std::string> artwork_urls;
 std::shared_ptr<const MetadataArtwork> artwork;
 double duration=0;
 std::map<std::string,std::string> fields;
 std::string playback_quality,metadata_source;
 std::uint64_t quality_time=0;
 bool Matches(const std::string&,const std::string&,const std::string&,double) const;
};
bool ParseRichMetadata(const std::string&,RichMetadata&,std::string&);
// Current-item footer also accepts episodes without music artist/album tags.
bool ParsePlaybackMetadata(const std::string&,RichMetadata&,std::string&);
bool MetadataTextValid(const std::string&);
bool ValidSpotifyAlbumUri(const std::string&) noexcept;
// Record state is protected by the metadata bridge lock. Matcher/save jobs pin
// the same record, so enrichment outlives playback changes and registry pruning.
struct MetadataRecord {
 RichMetadata value;
 bool local_attempted=false,queued=false;
 unsigned online_requested=0; // metadata=1, album=2, lyrics=4.
 std::string artwork_attempted;
};
class MetadataCache {
 std::deque<std::shared_ptr<MetadataRecord>> records_;
public:
 std::shared_ptr<MetadataRecord> Pin(const RichMetadata&);
 void Put(const RichMetadata&);
 const RichMetadata* FindUri(const std::string&) const;
 const RichMetadata* Find(const std::string&,const std::string&,const std::string&,double) const;
};
}
