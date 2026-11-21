#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace history {
inline constexpr size_t kArtworkReplyMaxImageBytes=4*1024*1024;
inline constexpr size_t kArtworkReplyMaxPayloadBytes=(kArtworkReplyMaxImageBytes*4+2)/3+4096;
struct ArtworkReply {
 std::string id,uri,reference,status,reason;
 unsigned source=0;
 std::vector<uint8_t> bytes;
};
// Parse the bounded renderer envelope, excluding FLOGGFY_ARTWORK_V1:.
// Data uses canonical unpadded base64url. Failure leaves out unchanged.
bool ParseArtworkReply(const std::string& input,ArtworkReply& out,std::string& error);
}
