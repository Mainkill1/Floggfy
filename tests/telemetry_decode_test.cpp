#include "../native/telemetry_decode.h"
#include "telemetry_decode_fixture.h"
#include <cassert>
#include <cstdio>
#include <string>
static std::string LengthField(std::string value){
 std::string field(1,'\x0a');size_t length=value.size();do{field+=char((length&127)|(length>=128?128:0));length>>=7;}while(length);return field+value;
}
int main(){
 using namespace telemetry_fixture;
 const auto gzip=Bytes(gzip_protobuf);
 auto decoded=privacy::DecodeTelemetryPayload(gzip,"application/x-protobuf","gzip");
 assert(decoded.decoded&&decoded.protobuf&&"gzip protobuf must become readable fields in the worker");
 assert(decoded.text.find("\"field\":1,\"wire\":2")!=std::string::npos&&decoded.text.find("ui.page_view")!=std::string::npos);
 assert(decoded.text.find("\"field\":2,\"wire\":0,\"unsigned\":\"42\"")!=std::string::npos);
 assert(decoded.text.find("possible_message")!=std::string::npos&&decoded.text.find("web-player")!=std::string::npos&&decoded.text.find("AP8=")!=std::string::npos);
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
 std::puts("PASS: gzip protobuf readable fields, checksums, consumption, malformed input and bounded expansion");
}
