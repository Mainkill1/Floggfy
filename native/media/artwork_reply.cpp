#include "spotify_uri.h"
#include "artwork_reply.h"
#include "artwork_url.h"
#include "../tooling/base64.h"
#include "../tooling/text_encoding.h"
#include <algorithm>
#include <array>
#include <string_view>
namespace history {
namespace {
bool DecodeHeader(std::string_view input,size_t limit,std::string& result){
 if(input.size()>limit*3)return false;
 for(size_t i=0;i<input.size();++i){
  char byte=input[i];
  if(byte=='%'&&!text_encoding::ReadPercentByte(input,i,byte))return false;
  const auto c=static_cast<unsigned char>(byte);
  if(c<32||c>126||result.size()==limit)return false;
  result+=char(c);
 }
 return true;
}
bool DecodeImage(std::string_view input,std::vector<uint8_t>& bytes){
 if(input.empty()||input.size()%4==1||input.size()*3/4>kArtworkReplyMaxImageBytes)return false;
 bytes.reserve(input.size()*3/4);
 uint32_t value=0;unsigned bits=0;
 for(char c:input){
  const int digit=base64::Digit(c,base64::Alphabet::Url);if(digit<0)return false;
  value=(value<<6)|unsigned(digit);bits+=6;
  if(bits>=8){bits-=8;bytes.push_back(uint8_t(value>>bits));}
 }
 // Unused low bits must be zero: reject alternate encodings of the same bytes.
 return !bits||(value&((1u<<bits)-1))==0;
}
}
bool ParseArtworkReply(const std::string& input,ArtworkReply& out,std::string& error){
 const auto fail=[&](const char* reason){error=reason;return false;};
 if(input.empty()||input.size()>kArtworkReplyMaxPayloadBytes)return fail("reply-size-invalid");
 constexpr std::array<std::string_view,7> names={"id","uri","reference","source","status","reason","data"};
 std::array<std::string_view,7> fields{};unsigned seen=0;
 const std::string_view text=input;
 for(size_t start=0;start<text.size();){
  auto end=text.find('&',start);if(end==text.npos)end=text.size();
  const auto equal=text.find('=',start);
  if(equal==text.npos||equal>=end)return fail("reply-pair-invalid");
  const auto name=text.substr(start,equal-start);const auto key=std::find(names.begin(),names.end(),name);
  if(key==names.end())return fail("reply-field-unknown");
  const auto index=size_t(key-names.begin());
  if(seen&(1u<<index))return fail("reply-field-duplicate");
  seen|=1u<<index;fields[index]=text.substr(equal+1,end-equal-1);
  if(end<text.size()&&end+1==text.size())return fail("reply-pair-invalid");
  start=end+1;
 }
 constexpr unsigned required=(1u<<0)|(1u<<1)|(1u<<3)|(1u<<4)|(1u<<5);
 if((seen&required)!=required)return fail("reply-field-missing");
 ArtworkReply next;std::string source;
 if(!DecodeHeader(fields[0],20,next.id)||!DecodeHeader(fields[1],38,next.uri)||
    !DecodeHeader(fields[2],64,next.reference)||!DecodeHeader(fields[3],1,source)||
    !DecodeHeader(fields[4],11,next.status)||!DecodeHeader(fields[5],64,next.reason))return fail("reply-field-invalid");
 if(next.id.empty()||!std::all_of(next.id.begin(),next.id.end(),[](char c){return c>='0'&&c<='9';})||!ValidSpotifyMediaUri(next.uri))return fail("reply-identity-invalid");
 if(!next.reference.empty()&&!IsAlbumArtworkUrl(next.reference))return fail("reply-reference-invalid");
 if(source.size()!=1||source[0]<'0'||source[0]>'3')return fail("reply-source-invalid");
 next.source=unsigned(source[0]-'0');
 if(next.reason.empty()||!std::all_of(next.reason.begin(),next.reason.end(),[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-';}))return fail("reply-reason-invalid");
 if(next.status=="ready"){
  if(next.reference.empty()||!DecodeImage(fields[6],next.bytes))return fail("reply-image-invalid");
 }else if(next.status!="unavailable"||!fields[6].empty())return fail("reply-status-invalid");
 out=std::move(next);error.clear();return true;
}
}
