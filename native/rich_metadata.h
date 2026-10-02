#pragma once
#include <string>
#include <cstdint>
#include <map>
#include <deque>
namespace history {
struct RichMetadata {
 std::string title,artist,album,uri;
 double duration=0;
 std::map<std::string,std::string> fields;
 std::string playback_quality;
 std::uint64_t quality_time=0;
 bool Matches(const std::string&,const std::string&,const std::string&,double) const;
};
bool ParseRichMetadata(const std::string&,RichMetadata&,std::string&);
bool MetadataTextValid(const std::string&);
class MetadataCache {
 std::deque<RichMetadata> records_;
public:
 void Put(const RichMetadata&);
 const RichMetadata* Find(const std::string&,const std::string&,const std::string&,double) const;
};
}
