#include "rich_metadata.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
namespace history {
static bool Utf8Valid(const std::string& s) {
 for(size_t i=0;i<s.size();) {
  unsigned char b=s[i++]; if(b<128) {if(!b) return false; continue;}
  unsigned n=b>=0xc2&&b<=0xdf?1:b>=0xe0&&b<=0xef?2:b>=0xf0&&b<=0xf4?3:99;
  if(n==99 || i+n>s.size()) return false;
  unsigned cp=b&((1u<<(6-n))-1);
  for(unsigned j=0;j<n;j++) {unsigned char t=s[i++]; if((t&0xc0)!=0x80) return false; cp=(cp<<6)|(t&63);}
  if((n==1&&cp<128)||(n==2&&cp<2048)||(n==3&&cp<65536)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)) return false;
 } return true;
}
static int Hex(char c) {return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static bool Decode(const std::string& s,std::string& out) {
 out.clear(); for(size_t i=0;i<s.size();i++) {
  if(s[i]=='%') {if(i+2>=s.size()||Hex(s[i+1])<0||Hex(s[i+2])<0) return false; out+=char(Hex(s[i+1])*16+Hex(s[i+2])); i+=2;}
  else out+=s[i];
 } return Utf8Valid(out);
}
bool RichMetadata::Matches(const std::string& t,const std::string& a,const std::string& al,double d) const {
 return title==t&&artist==a&&album==al&&std::fabs(duration-d)<=1.0;
}
bool ParseRichMetadata(const std::string& input,RichMetadata& out,std::string& error) {
 static const std::set<std::string> allowed={"v","title","artist","album","uri","duration","DATE","YEAR","GENRE","LYRICS","DISCNUMBER","DISCTOTAL","TRACKTOTAL","ISRC","PUBLISHER","LANGUAGE","COMMENT","SPOTIFY_URI"};
 auto fail=[&](const char* e){error=e; return false;};
 if(input.size()>131072) return fail("metadata too large");
 std::map<std::string,std::string> kv;
 for(size_t p=0;p<input.size();) {
  size_t end=input.find('&',p); if(end==std::string::npos) end=input.size();
  size_t eq=input.find('=',p); if(eq==std::string::npos||eq>=end) return fail("invalid pair");
  std::string key=input.substr(p,eq-p),value;
  if(!allowed.count(key)||kv.count(key)||!Decode(input.substr(eq+1,end-eq-1),value)) return fail("invalid field");
  if(value.size()>(key=="LYRICS"?65536u:4096u)) return fail("field too large");
  kv.emplace(key,std::move(value)); p=end+1;
 }
 if(kv["v"]!="1"||kv["title"].empty()||kv["artist"].empty()||kv["album"].empty()||kv["uri"].rfind("spotify:track:",0)!=0) return fail("missing identity");
 char* last=nullptr; double d=strtod(kv["duration"].c_str(),&last);
 if(!last||*last||!std::isfinite(d)||d<=0||d>86400) return fail("invalid duration");
 RichMetadata m; m.title=kv["title"];m.artist=kv["artist"];m.album=kv["album"];m.uri=kv["uri"];m.duration=d;
 for(auto& entry:kv) if(entry.first[0]>='A'&&entry.first[0]<='Z'&&!entry.second.empty()) m.fields.insert(entry);
 out=std::move(m); error.clear(); return true;
}
void MetadataCache::Put(const RichMetadata& m) {
 records_.erase(std::remove_if(records_.begin(),records_.end(),[&](const auto& r){return r.uri==m.uri;}),records_.end());
 records_.push_back(m); while(records_.size()>24) records_.pop_front();
}
const RichMetadata* MetadataCache::Find(const std::string& t,const std::string& a,const std::string& al,double d) const {
 const RichMetadata* result=nullptr;
 for(const auto& r:records_) if(r.Matches(t,a,al,d)) {if(result&&result->uri!=r.uri) return nullptr;result=&r;}
 return result;
}
}
