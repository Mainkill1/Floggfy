#include "../native/artwork_reply.h"
#include <cassert>
#include <iostream>
using namespace history;
static std::string Base64(const std::vector<uint8_t>& bytes) {
 static const char chars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
 std::string result;uint32_t value=0;unsigned bits=0;
 for(auto byte:bytes){value=(value<<8)|byte;bits+=8;while(bits>=6){bits-=6;result+=chars[(value>>bits)&63];}}
 if(bits)result+=chars[(value<<(6-bits))&63];
 return result;
}
int main() {
 const std::string uri="spotify:track:0000000000000000000001";
 const std::string reference="https://i.scdn.co/image/"+std::string(40,'a');
 const std::string header="id=42&uri=spotify%3Atrack%3A0000000000000000000001&reference="+reference+"&source=2&status=ready&reason=cache-hit&data=";
 ArtworkReply reply;std::string error;
 assert(ParseArtworkReply(header+"AP_-",reply,error));
 assert(error.empty()&&reply.id=="42"&&reply.uri==uri&&reply.reference==reference&&reply.source==2&&reply.status=="ready"&&reply.reason=="cache-hit");
 assert((reply.bytes==std::vector<uint8_t>{0,255,254}));
 for(const auto& bytes:{std::vector<uint8_t>{0},std::vector<uint8_t>{255,255},std::vector<uint8_t>{255,255,255}}) {
  assert(ParseArtworkReply(header+Base64(bytes),reply,error)&&reply.bytes==bytes);
 }
 const std::string miss="id=43&uri=spotify:episode:0000000000000000000002&source=0&status=unavailable&reason=cache-miss";
 assert(ParseArtworkReply(miss,reply,error)&&reply.bytes.empty()&&reply.reference.empty()&&reply.source==0);
 auto fails=[&](const std::string& input){
  ArtworkReply sentinel;sentinel.id="unchanged";sentinel.bytes={7,8};reply=sentinel;
  assert(!ParseArtworkReply(input,reply,error)&&!error.empty());
  assert(reply.id==sentinel.id&&reply.bytes==sentinel.bytes);
 };
 for(const auto value:{"", "A", "AB", "AAF", "AA=", "AA==", "++__", "//__", "AA%3D", "AA\n"})fails(header+value);
 fails(header+"AA&extra=1");fails(header+"AA&id=43");fails(header+"AA&");
 fails(miss+"&data=AA");fails(miss+"&reference=https://example.com/art");
 fails("id=43&uri="+uri+"&status=unavailable&reason=cache-miss");
 for(const auto value:{"-1","1.2","","123456789012345678901","%00","%G0"})fails("id="+std::string(value)+"&uri="+uri+"&source=0&status=unavailable&reason=cache-miss");
 for(const auto value:{"spotify:track:short","spotify:album:0000000000000000000001","spotify:track:000000000000000000000!"})fails("id=1&uri="+std::string(value)+"&source=0&status=unavailable&reason=cache-miss");
 for(const auto value:{"4","-1","00","%FF"})fails("id=1&uri="+uri+"&source="+value+"&status=unavailable&reason=cache-miss");
 fails("id=1&uri="+uri+"&source=0&status=ready&reason=cache-hit&data=AA");
 fails("id=1&uri="+uri+"&source=0&status=invalid&reason=cache-miss");
 fails("id=1&uri="+uri+"&source=0&status=unavailable&reason="+std::string(65,'a'));
 fails("id=1&uri="+uri+"&source=0&status=unavailable&reason=two%20words");
 fails("id=1&uri="+uri+"&source=0&status=unavailable&reason=");
 std::vector<uint8_t> largest(kArtworkReplyMaxImageBytes,255);
 assert(ParseArtworkReply(header+Base64(largest),reply,error)&&reply.bytes==largest);
 largest.push_back(0);fails(header+Base64(largest));
 fails(std::string(kArtworkReplyMaxPayloadBytes+1,'x'));
 std::cout<<"PASS: artwork reply identity, canonical base64url, malformed fields, size bounds and unchanged output on failure\n";
}
