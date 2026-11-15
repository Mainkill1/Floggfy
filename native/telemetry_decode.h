#pragma once
#include <string>
#include <string_view>
namespace privacy {
// Worker-only, portable decoding. text is decoded bytes or generic protobuf
// JSON. detail is a fixed status label, never derived from request contents.
struct PayloadDecode {
 bool decoded=false,protobuf=false;
 size_t decoded_bytes=0;
 std::string text;
 const char* detail="unsupported-content-format";
};
PayloadDecode DecodeTelemetryPayload(std::string_view body,std::string_view content_type,std::string_view content_encoding);
}
