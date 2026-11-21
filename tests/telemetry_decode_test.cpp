#include "../native/telemetry/telemetry_decode.h"
#include "telemetry_decode_fixture.h"
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <string>
static std::string LengthField(std::string value){
 std::string field(1,'\x0a');size_t length=value.size();do{field+=char((length&127)|(length>=128?128:0));length>>=7;}while(length);return field+value;
}
static std::string V(uint64_t value){std::string out;do{out+=char((value&127)|(value>=128?128:0));value>>=7;}while(value);return out;}
static std::string B(unsigned number,const std::string& value){return V((uint64_t(number)<<3)|2)+V(value.size())+value;}
static std::string I(unsigned number,uint64_t value){return V(uint64_t(number)<<3)+V(value);}
static std::string Fragment(const std::string& name,const std::string& data){return B(1,name)+B(2,data);}
static size_t Occurrences(const std::string& text,const std::string& value){size_t result=0,at=0;while((at=text.find(value,at))!=text.npos){++result;at+=value.size();}return result;}
int main(){
 using namespace telemetry_fixture;
 const auto gzip=Bytes(gzip_protobuf);
 auto decoded=privacy::DecodeTelemetryPayload(gzip,"application/x-protobuf","gzip");
 assert(decoded.decoded&&decoded.protobuf&&"gzip protobuf must become readable fields in the worker");
 assert(decoded.text.find("\"field\":1,\"wire\":2")!=std::string::npos&&decoded.text.find("ui.page_view")!=std::string::npos);
 assert(decoded.text.find("\"field\":2,\"wire\":0,\"unsigned\":\"42\"")!=std::string::npos);
 assert(decoded.text.find("possible_message")!=std::string::npos&&decoded.text.find("web-player")!=std::string::npos&&decoded.text.find("AP8=")!=std::string::npos);
  const auto opaque=decoded.text.find("\"bytes_base64\"");
  assert(opaque!=std::string::npos&&decoded.text.find("\"bytes_base64\"",opaque+1)==std::string::npos&&
      "successfully decoded nested messages must not repeat their original base64 bytes");
  assert(decoded.text.find("opaque binary; not readable UTF-8 or a recognized nested message")!=std::string::npos);
 assert(privacy::DecodeTelemetryPayload(Bytes(protobuf),"application/x-protobuf; version=1","identity").text==decoded.text);
 const auto text_body=std::string("\x0a\x80\x40",3)+std::string(8192,'0');
 assert(privacy::DecodeTelemetryPayload(text_body,"application/x-protobuf","").decoded&&"plain UTF-8 text must not consume nested-message node budget");

 assert(privacy::DecodeTelemetryPayload(gzip,"application/octet-stream"," GZip ").text==Bytes(protobuf));
 assert(privacy::DecodeTelemetryPayload(Bytes(gzip_optional_header),"application/x-protobuf","gzip").text==decoded.text);
 auto header_corrupt=Bytes(gzip_optional_header);header_corrupt[30]^=1;
 const auto header_error=privacy::DecodeTelemetryPayload(header_corrupt,"application/x-protobuf","gzip");assert(!header_error.decoded&&std::string(header_error.detail)=="gzip-header-checksum-mismatch");
 auto corrupt=gzip;corrupt[corrupt.size()-8]^=1;
 auto error=privacy::DecodeTelemetryPayload(corrupt,"application/x-protobuf","gzip");assert(!error.decoded&&std::string(error.detail)=="gzip-checksum-mismatch");
 corrupt=gzip;corrupt.back()^=1;error=privacy::DecodeTelemetryPayload(corrupt,"application/x-protobuf","gzip");assert(!error.decoded);
 corrupt=gzip;corrupt.insert(corrupt.end()-8,0);error=privacy::DecodeTelemetryPayload(corrupt,"application/x-protobuf","gzip");assert(!error.decoded&&std::string(error.detail)=="gzip-trailing-deflate-data");
 corrupt=gzip.substr(0,gzip.size()-3);assert(!privacy::DecodeTelemetryPayload(corrupt,"application/x-protobuf","gzip").decoded);
 error=privacy::DecodeTelemetryPayload(Bytes(expansion_gzip),"application/x-protobuf","gzip");assert(!error.decoded&&std::string(error.detail)=="gzip-expansion-limit");
 corrupt=Bytes(expansion_gzip);for(size_t i=corrupt.size()-4;i<corrupt.size();++i)corrupt[i]=0;
 error=privacy::DecodeTelemetryPayload(corrupt,"application/x-protobuf","gzip");assert(!error.decoded&&std::string(error.detail)=="gzip-expansion-limit");
 assert(!privacy::DecodeTelemetryPayload(gzip,"application/x-protobuf","br").decoded);
 assert(!privacy::DecodeTelemetryPayload(std::string("\x0a\x05x",3),"application/x-protobuf","").decoded);

 std::string many;for(unsigned i=0;i<5000;++i)many.append("\x08\x01",2);
 error=privacy::DecodeTelemetryPayload(many,"application/x-protobuf","");assert(!error.decoded&&std::string(error.detail)=="protobuf-node-limit");
 auto deep=std::string("\x08\x01",2);for(unsigned i=0;i<10;++i)deep=LengthField(deep);
 error=privacy::DecodeTelemetryPayload(deep,"application/x-protobuf","");assert(!error.decoded&&std::string(error.detail)=="protobuf-depth-limit");
 error=privacy::DecodeTelemetryPayload(LengthField(std::string(300000,'"')),"application/x-protobuf","");assert(!error.decoded&&std::string(error.detail)=="protobuf-output-limit");
 error=privacy::DecodeTelemetryPayload(std::string(1024*1024+1,'x'),"application/x-protobuf","");assert(!error.decoded&&std::string(error.detail)=="input-limit");
 auto numeric=std::string("\x08",1)+std::string(9,char(0xff))+std::string("\x01\x15\x78\x56\x34\x12\x19\xef\xcd\xab\x89\x67\x45\x23\x01",15);
 decoded=privacy::DecodeTelemetryPayload(numeric,"application/x-protobuf","");
 assert(decoded.decoded&&decoded.text.find("18446744073709551615")!=decoded.text.npos&&decoded.text.find("0x12345678")!=decoded.text.npos&&decoded.text.find("0x0123456789abcdef")!=decoded.text.npos);
 corrupt=gzip;corrupt[3]|=0x20;assert(!privacy::DecodeTelemetryPayload(corrupt,"application/x-protobuf","gzip").decoded);
 corrupt=gzip;corrupt[corrupt.size()-4]^=1;error=privacy::DecodeTelemetryPayload(corrupt,"application/x-protobuf","gzip");assert(!error.decoded&&std::string(error.detail)=="gzip-size-mismatch");
 corrupt=gzip;corrupt[10]=7;error=privacy::DecodeTelemetryPayload(corrupt,"application/x-protobuf","gzip");assert(!error.decoded&&std::string(error.detail)=="gzip-invalid-deflate");
 assert(!privacy::DecodeTelemetryPayload(gzip+gzip,"application/x-protobuf","gzip").decoded);

 // Verified envelope and event descriptors replace generic field walls with named data.
 const auto stats=B(1,I(1,1))+B(2,"Canary")+B(2,"Other")+B(3,V(1)+V(2)+V(3))+I(3,4)+B(5,V(6)+V(7))+I(99,8);
 auto event=B(2,"EventSenderStats2NonAuth")+B(3,Fragment("message",stats));
 event+=B(3,Fragment("context_time",I(1,123)))+B(3,Fragment("context_time",I(1,456)));
 event+=B(3,Fragment("context_client_id",B(1,I(1,1))))+B(3,B(1,"context_client_context_id"));
 event+=B(3,Fragment("new_context",I(1,9)))+B(4,I(1,1))+I(5,2);
 const auto unknown=B(2,"FutureEvent")+B(3,Fragment("message",I(7,9)));
 decoded=privacy::DecodeTelemetryPayload(B(1,event)+B(1,unknown)+I(2,1),"application/x-protobuf","",true);
 assert(decoded.decoded&&decoded.text.find("\"schema\":\"spotify.event_sender.proto.PublishEventsRequest\"")!=decoded.text.npos&&"verified envelope must use named fields");
 assert(decoded.text.find("\"event\":[")!=decoded.text.npos&&decoded.text.find("\"event_fragment\":[")!=decoded.text.npos&&decoded.text.find("\"suppress_persist\":true")!=decoded.text.npos);
 assert(decoded.text.find("\"event_names\":[\"Canary\",\"Other\"]")!=decoded.text.npos);
 assert(decoded.text.find("\"loss_stats_num_entries_per_sequence_id\":[1,2,3,4]")!=decoded.text.npos);
 assert(decoded.text.find("\"loss_stats_storage_sizes\":[\"6\",\"7\"]")!=decoded.text.npos);
 assert(Occurrences(decoded.text,"\"name\":\"context_time\"")==2&&decoded.text.find("\"value\":\"123\"")!=decoded.text.npos&&decoded.text.find("\"value\":\"456\"")!=decoded.text.npos);
 assert(decoded.text.find("\"value\":{\"bytes_base64\":\"CAE=\"}")!=decoded.text.npos&&decoded.text.find("possible_message")==decoded.text.npos);
 assert(decoded.text.find("FutureEvent")!=decoded.text.npos&&decoded.text.find("\"field\":7,\"wire\":0")!=decoded.text.npos&&decoded.text.find("\"field\":99,\"wire\":0")!=decoded.text.npos);
 const auto invalid_layout=B(1,B(2,"EventSenderStats2NonAuth")+I(3,1));
 decoded=privacy::DecodeTelemetryPayload(invalid_layout,"application/x-protobuf","",true);assert(decoded.decoded&&decoded.text.find("schema unknown")!=decoded.text.npos);

 // Names can follow data on the wire; dispatch must not depend on serialization order.
 const auto canary=B(9,"variant-a")+I(3,2)+I(2,UINT64_MAX)+I(5,0)+B(1,"first")+B(1,"second");
 const auto reordered=B(1,B(3,B(2,canary)+B(1,"message"))+B(2,"EventSenderCanaryNonAuth"));
 decoded=privacy::DecodeTelemetryPayload(reordered,"application/x-protobuf","",true);
 assert(decoded.decoded&&decoded.text.find("\"experiment_variant\":\"variant-a\"")!=decoded.text.npos);
 assert(decoded.text.find("\"batch_size\":\"2\"")!=decoded.text.npos&&decoded.text.find("\"event_id\":\"-1\"")!=decoded.text.npos&&decoded.text.find("\"sent_as_pending_event\":false")!=decoded.text.npos);
 assert(decoded.text.find("\"batch_uuid\":[\"first\",\"second\"]")!=decoded.text.npos&&"duplicate singular fields must not silently overwrite earlier values");
 decoded=privacy::DecodeTelemetryPayload(reordered,"application/x-protobuf","");
 assert(decoded.decoded&&decoded.text.find("schema unknown")!=decoded.text.npos&&decoded.text.find("experiment_variant")==decoded.text.npos&&"named decoding needs the explicit Gabo route hint");
 // A known field with an incompatible wire type is retained under its number.
 const auto mismatch=B(1,B(2,"EventSenderCanaryNonAuth")+B(3,Fragment("message",I(9,1))));
 decoded=privacy::DecodeTelemetryPayload(mismatch,"application/x-protobuf","",true);
 assert(decoded.decoded&&decoded.text.find("experiment_variant")==decoded.text.npos&&decoded.text.find("\"field\":9,\"wire\":0")!=decoded.text.npos);
 std::puts("PASS: gzip protobuf readable fields, checksums, consumption, malformed input and bounded expansion");
}
