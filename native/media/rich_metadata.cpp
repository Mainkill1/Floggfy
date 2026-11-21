#include "rich_metadata.h"
#include "artwork_url.h"
#include "../tooling/text_encoding.h"
#include <algorithm>
#include <iterator>
#include <cmath>
#include <cstdlib>
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
bool ParseRichMetadata(std::string_view input,RichMetadata& out,std::string& error) {
 static constexpr std::string_view keys[]={
  "v","title","artist","album","uri","duration","album_uri","artwork_url","artwork_urls","metadata_source","playback_quality",
  "ARTIST","ALBUMARTIST","DATE","YEAR","GENRE","LYRICS","TRACKNUMBER","DISCNUMBER","DISCTOTAL","TRACKTOTAL","ISRC",
  "LABEL","ORGANIZATION","PUBLISHER","COPYRIGHT","PHONOGRAMCOPYRIGHT","COMPOSER","REMIXER","CONDUCTOR","LANGUAGE",
  "COMMENT","SPOTIFY_URI","SPOTIFY_ALBUM_URI"};
 static_assert(std::size(keys)<=64);
 auto fail=[&](const char* e){error=e;return false;};
 if(input.size()>131072)return fail("metadata too large");
 RichMetadata m;
 std::string version,duration,artwork_urls;
 std::string* const controls[]={&version,&m.title,&m.artist,&m.album,&m.uri,&duration,&m.album_uri,
                                &m.artwork_url,&artwork_urls,&m.metadata_source,&m.playback_quality};
 uint64_t seen=0;
 for(size_t p=0;p<input.size();) {
  auto end=input.find('&',p);if(end==input.npos)end=input.size();
  const auto eq=input.find('=',p);if(eq>=end)return fail("invalid pair");
  const auto key=input.substr(p,eq-p);
  const auto field=std::find(std::begin(keys),std::end(keys),key);
  if(field==std::end(keys))return fail("invalid field");
  const auto slot=size_t(field-std::begin(keys));
  const auto mask=uint64_t{1}<<slot;
  std::string value;
  if((seen&mask)||!DecodeMetadataText(input.substr(eq+1,end-eq-1),value))return fail("invalid field");
  if(value.size()>(key=="LYRICS"?65536u:4096u))return fail("field too large");
  seen|=mask;
  if(slot<std::size(controls))*controls[slot]=std::move(value);
  else if(!value.empty())m.fields.emplace(key,std::move(value));
  p=end+1;
 }
 const bool episode=m.uri.rfind("spotify:episode:",0)==0,track=m.uri.rfind("spotify:track:",0)==0;
 if(version!="1"||m.title.empty()||(!track&&!episode)||(!episode&&(m.artist.empty()||m.album.empty())))
  return fail("missing identity");
 const auto alternative=m.fields.find("SPOTIFY_ALBUM_URI");
 if(alternative!=m.fields.end()) {
  if(m.album_uri.empty())m.album_uri=alternative->second;
  else if(m.album_uri!=alternative->second)return fail("invalid album identity");
 }
 if(!m.album_uri.empty()&&(!track||!ValidSpotifyUri(m.album_uri,"spotify:album:")))return fail("invalid album identity");
 char* last=nullptr;m.duration=strtod(duration.c_str(),&last);
 if(!last||*last||!std::isfinite(m.duration)||m.duration<=0||m.duration>86400)return fail("invalid duration");
 if(m.metadata_source!="online")std::string().swap(m.metadata_source);
 const std::string_view references=artwork_urls;
 for(size_t start=0;start<references.size()&&m.artwork_urls.size()<8;) {
  auto end=references.find('\n',start);if(end==references.npos)end=references.size();
  const auto reference=references.substr(start,end-start);
  if(IsAlbumArtworkUrl(reference)&&
     std::find(m.artwork_urls.begin(),m.artwork_urls.end(),reference)==m.artwork_urls.end())m.artwork_urls.emplace_back(reference);
  start=end+1;
 }
 static constexpr std::string_view levels[]={"low","normal","high","very_high","lossless","lossless_24","hifi"};
 if(std::find(std::begin(levels),std::end(levels),m.playback_quality)==std::end(levels))std::string().swap(m.playback_quality);
 if(!m.album_uri.empty())m.fields["SPOTIFY_ALBUM_URI"]=m.album_uri;
 out=std::move(m);error.clear();return true;
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
