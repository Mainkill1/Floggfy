#include "rich_metadata.h"
#include "artwork_url.h"
#include "../tooling/text_encoding.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
namespace history {
bool MetadataTextValid(std::string_view text) {
 return text_encoding::ValidUtf8(text,text_encoding::Ascii::NoNull);
}
bool DecodeMetadataText(std::string_view text,std::string& out) {
 out.clear();
 for(size_t i=0;i<text.size();++i) {
  char byte=text[i];
  if(byte=='%'&&!text_encoding::ReadPercentByte(text,i,byte))return false;
  out+=byte;
 }
 return MetadataTextValid(out);
}
bool RichMetadata::Matches(const std::string& t,const std::string& a,const std::string& al,double d) const {
 return title==t&&artist==a&&album==al&&std::fabs(duration-d)<=1.0;
}
bool ParseRichMetadata(const std::string& input,RichMetadata& out,std::string& error) {
 static const std::set<std::string> allowed={"v","artwork_url","artwork_urls","metadata_source","playback_quality","title","artist","album","uri","album_uri","duration","ARTIST","ALBUMARTIST","DATE","YEAR","GENRE","LYRICS","TRACKNUMBER","DISCNUMBER","DISCTOTAL","TRACKTOTAL","ISRC","LABEL","ORGANIZATION","PUBLISHER","COPYRIGHT","PHONOGRAMCOPYRIGHT","COMPOSER","REMIXER","CONDUCTOR","LANGUAGE","COMMENT","SPOTIFY_URI","SPOTIFY_ALBUM_URI"};
 auto fail=[&](const char* e){error=e; return false;};
 const std::string_view text=input;
 if(input.size()>131072) return fail("metadata too large");
 std::map<std::string,std::string> kv;
 for(size_t p=0;p<input.size();) {
  size_t end=input.find('&',p); if(end==std::string::npos) end=input.size();
  size_t eq=input.find('=',p); if(eq==std::string::npos||eq>=end) return fail("invalid pair");
  std::string key=input.substr(p,eq-p),value;
  if(!allowed.count(key)||kv.count(key)||!DecodeMetadataText(text.substr(eq+1,end-eq-1),value)) return fail("invalid field");
  if(value.size()>(key=="LYRICS"?65536u:4096u)) return fail("field too large");
  kv.emplace(key,std::move(value)); p=end+1;
 }
 const bool episode=kv["uri"].rfind("spotify:episode:",0)==0;
 const bool track=kv["uri"].rfind("spotify:track:",0)==0;
 const bool labels=episode||(!kv["artist"].empty()&&!kv["album"].empty());
 if(kv["v"]!="1"||kv["title"].empty()||(!track&&!episode)||!labels) return fail("missing identity");
 const auto album_uri=!kv["album_uri"].empty()?kv["album_uri"]:kv["SPOTIFY_ALBUM_URI"];
 if((!album_uri.empty()&&(!track||!ValidSpotifyUri(album_uri,"spotify:album:")))||
    (!kv["album_uri"].empty()&&!kv["SPOTIFY_ALBUM_URI"].empty()&&kv["album_uri"]!=kv["SPOTIFY_ALBUM_URI"]))
  return fail("invalid album identity");
 char* last=nullptr; double d=strtod(kv["duration"].c_str(),&last);
 if(!last||*last||!std::isfinite(d)||d<=0||d>86400) return fail("invalid duration");
 RichMetadata m; m.title=kv["title"];m.artist=kv["artist"];m.album=kv["album"];m.uri=kv["uri"];m.album_uri=album_uri;m.duration=d;m.artwork_url=kv["artwork_url"];
 if(kv["metadata_source"]=="online")m.metadata_source="online";
 const auto references=kv["artwork_urls"];
 for(size_t start=0;start<references.size();) {
  auto end=references.find('\n',start);if(end==std::string::npos)end=references.size();
  auto reference=references.substr(start,end-start);
  if(m.artwork_urls.size()>=8)break;
  if(IsAlbumArtworkUrl(reference)&&
     std::find(m.artwork_urls.begin(),m.artwork_urls.end(),reference)==m.artwork_urls.end())m.artwork_urls.push_back(std::move(reference));
  start=end+1;
 }
 static const std::set<std::string> levels={"low","normal","high","very_high","lossless","lossless_24","hifi"};
 if(levels.count(kv["playback_quality"]))m.playback_quality=kv["playback_quality"];
 for(auto& entry:kv) if(entry.first[0]>='A'&&entry.first[0]<='Z'&&!entry.second.empty()) m.fields.insert(entry);
 if(!m.album_uri.empty())m.fields["SPOTIFY_ALBUM_URI"]=m.album_uri;
 out=std::move(m); error.clear(); return true;
}
std::shared_ptr<MetadataRecord> MetadataCache::Pin(const RichMetadata& m) {
 for(const auto& record:records_)if(record->value.uri==m.uri)return record;
 auto record=std::make_shared<MetadataRecord>();record->value=m;
 records_.push_back(record);
 // Unowned history is only a bounded hand-off window for renderer messages
 // preceding playback observations. Pinned recording/job records cannot expire.
 size_t unowned=0;
 for(const auto& item:records_)if(item.use_count()==1)++unowned;
 for(auto it=records_.begin();it!=records_.end()&&unowned>24;)
  if(it->use_count()==1){it=records_.erase(it);--unowned;}else ++it;
 return record;
}
void MetadataCache::Put(const RichMetadata& m) {Pin(m)->value=m;}
std::vector<std::shared_ptr<MetadataRecord>> MetadataCache::Pinned() const {
 std::vector<std::shared_ptr<MetadataRecord>> pinned;
 for(const auto& record:records_)if(record.use_count()>1)pinned.push_back(record);
 return pinned;
}
const RichMetadata* MetadataCache::FindUri(const std::string& uri) const {
 for(const auto& record:records_)if(record->value.uri==uri)return &record->value;
 return nullptr;
}
const RichMetadata* MetadataCache::Find(const std::string& t,const std::string& a,const std::string& al,double d) const {
 const RichMetadata* result=nullptr;
 for(const auto& record:records_) {const auto& r=record->value;
  if(r.Matches(t,a,al,d)) {if(result&&result->uri!=r.uri)return nullptr;result=&r;}
 }
 return result;
}
}
