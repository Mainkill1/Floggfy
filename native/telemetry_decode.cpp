#include "telemetry_decode.h"
#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>
extern "C" {
#include "vendor/puff/puff.h"
}
namespace privacy {
namespace {
constexpr size_t kExpanded=1024*1024,kOutput=512*1024;
constexpr unsigned kDepth=8,kNodes=4096;
unsigned Byte(char value){return static_cast<unsigned char>(value);}
uint32_t Little32(std::string_view bytes,size_t offset){
 return Byte(bytes[offset])|(Byte(bytes[offset+1])<<8)|(Byte(bytes[offset+2])<<16)|(Byte(bytes[offset+3])<<24);
}
uint32_t Crc32(std::string_view bytes){
 uint32_t crc=0xffffffffu;
 for(char c:bytes){crc^=Byte(c);for(unsigned i=0;i<8;++i)crc=(crc>>1)^(0xedb88320u&uint32_t(0-int(crc&1)));}
 return ~crc;
}
std::string LowerTrim(std::string_view value){
 const auto semi=value.find(';');if(semi!=value.npos)value=value.substr(0,semi);
 while(!value.empty()&&(value.front()==' '||value.front()=='\t'))value.remove_prefix(1);
 while(!value.empty()&&(value.back()==' '||value.back()=='\t'))value.remove_suffix(1);
 std::string out(value);for(auto& c:out)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return out;
}
bool InflateGzip(std::string_view input,std::string& output,const char*& detail){
 if(input.size()<18||Byte(input[0])!=0x1f||Byte(input[1])!=0x8b||Byte(input[2])!=8||(Byte(input[3])&0xe0)){detail="gzip-invalid-header";return false;}
 const auto trailer=input.size()-8;size_t position=10;const auto flags=Byte(input[3]);
 if(flags&4){
  if(position+2>trailer){detail="gzip-truncated-header";return false;}
  const auto extra=Byte(input[position])|(Byte(input[position+1])<<8);position+=2;
  if(extra>trailer-position){detail="gzip-truncated-header";return false;}position+=extra;
 }
 for(unsigned mask:{8u,16u})if(flags&mask){
  while(position<trailer&&input[position])++position;
  if(position==trailer){detail="gzip-truncated-header";return false;}++position;
 }
 if(flags&2){
  if(position+2>trailer){detail="gzip-truncated-header";return false;}
  const auto expected=Byte(input[position])|(Byte(input[position+1])<<8);
  if((Crc32(input.substr(0,position))&0xffff)!=expected){detail="gzip-header-checksum-mismatch";return false;}position+=2;
 }
 const auto expected_size=Little32(input,trailer+4);
 if(expected_size>kExpanded){detail="gzip-expansion-limit";return false;}
 output.resize(kExpanded);unsigned long output_size=static_cast<unsigned long>(output.size()),input_size=static_cast<unsigned long>(trailer-position);
 const auto status=puff(reinterpret_cast<unsigned char*>(output.data()),&output_size,reinterpret_cast<const unsigned char*>(input.data()+position),&input_size);
 if(status){output.clear();detail=status==1?"gzip-expansion-limit":status==2?"gzip-truncated-deflate":"gzip-invalid-deflate";return false;}
 if(input_size!=trailer-position){output.clear();detail="gzip-trailing-deflate-data";return false;}
 output.resize(output_size);
 if(output.size()!=expected_size){output.clear();detail="gzip-size-mismatch";return false;}
 if(Crc32(output)!=Little32(input,trailer)){output.clear();detail="gzip-checksum-mismatch";return false;}
 return true;
}
bool Text(std::string_view input){
 for(size_t i=0;i<input.size();){
  unsigned c=Byte(input[i++]);
  if(c<128){if(c<32&&c!='\n'&&c!='\r'&&c!='\t')return false;if(c==127)return false;continue;}
  unsigned value=0,count=0,minimum=0;
  if(c>=0xc2&&c<=0xdf){value=c&31;count=1;minimum=0x80;}
  else if(c>=0xe0&&c<=0xef){value=c&15;count=2;minimum=0x800;}
  else if(c>=0xf0&&c<=0xf4){value=c&7;count=3;minimum=0x10000;}
  else return false;
  if(count>input.size()-i)return false;
  while(count--){c=Byte(input[i++]);if((c&0xc0)!=0x80)return false;value=(value<<6)|(c&63);}
  if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return false;
 }
 return true;
}
bool Varint(std::string_view input,size_t& position,uint64_t& value){
 value=0;
 for(unsigned i=0;i<10;++i){
  if(position==input.size())return false;
  const auto c=Byte(input[position++]);
  if(i==9&&c>1)return false;
  value|=uint64_t(c&127)<<(i*7);if(!(c&128))return true;
 }return false;
}
class Protobuf {
 unsigned nodes=0;
 bool Add(std::string& out,std::string_view text){if(text.size()>kOutput-out.size()){detail="protobuf-output-limit";return false;}out+=text;return true;}
 bool JsonString(std::string& out,std::string_view text){
  if(!Add(out,"\""))return false;
  for(char c:text){
   if(c=='"'||c=='\\'){const char escaped[]={'\\',c};if(!Add(out,{escaped,2}))return false;}
   else if(Byte(c)<32){char escaped[7];std::snprintf(escaped,sizeof(escaped),"\\u%04x",Byte(c));if(!Add(out,escaped))return false;}
   else if(!Add(out,{&c,1}))return false;
  }return Add(out,"\"");
 }
 bool Base64(std::string& out,std::string_view bytes){
  static constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if(!Add(out,"\""))return false;
  for(size_t i=0;i<bytes.size();i+=3){
   const unsigned value=(Byte(bytes[i])<<16)|(i+1<bytes.size()?Byte(bytes[i+1])<<8:0)|(i+2<bytes.size()?Byte(bytes[i+2]):0);
   const char chunk[]={alphabet[(value>>18)&63],alphabet[(value>>12)&63],i+1<bytes.size()?alphabet[(value>>6)&63]:'=',i+2<bytes.size()?alphabet[value&63]:'='};
   if(!Add(out,{chunk,4}))return false;
  }return Add(out,"\"");
 }
 bool Message(std::string_view input,std::string& out,unsigned depth){
  if(depth>kDepth){detail="protobuf-depth-limit";return false;}
  if(!Add(out,"{\"fields\":["))return false;
  size_t position=0;bool first=true;
  while(position<input.size()){
   if(++nodes>kNodes){detail="protobuf-node-limit";return false;}
   uint64_t tag=0;if(!Varint(input,position,tag)||!(tag>>3)||(tag>>3)>0x1fffffffu){detail="protobuf-invalid-tag";return false;}
   const auto wire=unsigned(tag&7);
   if(!first&&!Add(out,","))return false;
   first=false;
   if(!Add(out,"{\"field\":"+std::to_string(tag>>3)+",\"wire\":"+std::to_string(wire)))return false;
   if(wire==0){
    uint64_t value=0;if(!Varint(input,position,value)){detail="protobuf-invalid-varint";return false;}
    if(!Add(out,",\"unsigned\":\""+std::to_string(value)+"\""))return false;
   }else if(wire==1||wire==5){
    const size_t width=wire==1?8:4;if(width>input.size()-position){detail="protobuf-truncated-fixed";return false;}
    uint64_t value=0;for(size_t i=0;i<width;++i)value|=uint64_t(Byte(input[position++]))<<(i*8);
    char hex[24];std::snprintf(hex,sizeof(hex),"0x%0*llx",int(width*2),static_cast<unsigned long long>(value));
    if(!Add(out,",\"bits_hex\":\"")||!Add(out,hex)||!Add(out,"\""))return false;
   }else if(wire==2){
    uint64_t length=0;if(!Varint(input,position,length)||length>input.size()-position){detail="protobuf-truncated-bytes";return false;}
    const auto bytes=input.substr(position,size_t(length));position+=size_t(length);
    if(!Add(out,",\"length\":"+std::to_string(length)))return false;
    const bool text=Text(bytes);if(text&&(!Add(out,",\"text\":")||!JsonString(out,bytes)))return false;
    bool nested=false;
    // Length-delimited data has no schema: a nested parse is only a possibility.
    // Preserve bytes alongside it so the interpretation never loses information.
    if(!text&&!bytes.empty()){
     const auto candidate=Byte(bytes[0])&7;
     if(candidate==0||candidate==1||candidate==2||candidate==5){
      std::string child;const auto previous=detail;
      nested=Message(bytes,child,depth+1);
      if(nested){if(!Add(out,",\"possible_message\":")||!Add(out,child))return false;}
      else if(std::string_view(detail).find("-limit")!=std::string_view::npos)return false;
      detail=previous;
     }
    }
    if((!text||nested)&&(!Add(out,",\"bytes_base64\":")||!Base64(out,bytes)))return false;
   }else {detail="protobuf-unsupported-wire-type";return false;}
   if(!Add(out,"}"))return false;
  }
  return Add(out,"]}");
 }
public:
 const char* detail="protobuf-invalid-message";
 bool Decode(std::string_view input,std::string& output){
  std::string message;if(!Message(input,message,0))return false;
  return Add(output,"{\"format\":\"protobuf wire fields; schema unknown\",\"message\":")&&Add(output,message)&&Add(output,"}");
 }
};
}
PayloadDecode DecodeTelemetryPayload(std::string_view body,std::string_view content_type,std::string_view content_encoding){
 PayloadDecode result;
 if(body.size()>kExpanded){result.detail="input-limit";return result;}
 const auto type=LowerTrim(content_type),encoding=LowerTrim(content_encoding);
 const bool gzip=encoding=="gzip",protobuf=type=="application/x-protobuf"||type=="application/protobuf"||type=="application/vnd.google.protobuf";
 if(!gzip&&!encoding.empty()&&encoding!="identity"){result.detail="unsupported-content-encoding";return result;}
 if(!gzip&&!protobuf)return result;
 std::string expanded;
 if(gzip){if(!InflateGzip(body,expanded,result.detail))return result;body=expanded;}
 result.decoded_bytes=body.size();
 if(protobuf){
  Protobuf parser;if(!parser.Decode(body,result.text)){result.text.clear();result.detail=parser.detail;return result;}
  result.protobuf=true;result.detail=gzip?"gzip+protobuf-wire-fields":"protobuf-wire-fields";
 }else{result.text.assign(body);result.detail="gzip-decoded";}
 result.decoded=true;return result;
}
}
