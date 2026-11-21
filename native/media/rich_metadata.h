#pragma once
#include "spotify_uri.h"
#include <string>
#include <string_view>
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
bool DecodeMetadataText(std::string_view encoded,std::string& out);
bool MetadataTextValid(std::string_view);
// Record state is protected by the metadata bridge lock. Matcher/save jobs pin
// the same record, so enrichment outlives playback changes and registry pruning.
struct MetadataRecord {
 RichMetadata value;
 bool local_attempted=false,queued=false;
 unsigned online_requested=0; // metadata=1, album=2, lyrics=4.
 std::vector<std::string> cached_artwork_attempted,native_artwork_attempted;
 std::string online_artwork_attempted;
};
class MetadataCache {
 std::deque<std::shared_ptr<MetadataRecord>> records_;
public:
 std::shared_ptr<MetadataRecord> Pin(const RichMetadata&);
 void Put(const RichMetadata&);
 std::vector<std::shared_ptr<MetadataRecord>> Pinned() const;
 const RichMetadata* FindUri(const std::string&) const;
 const RichMetadata* Find(const std::string&,const std::string&,const std::string&,double) const;
};
}
