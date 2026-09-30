#include "compressed_buffer.h"
#include <algorithm>
#include <cstring>
#include <limits>
namespace history {
bool CompressedBuffer::Append(const uint8_t* bytes,size_t count,size_t available) {
    if(count && !bytes) return false;
    if(count>std::numeric_limits<size_t>::max()-length-block_size) return false;
    size_t required=(length+count+block_size-1)/block_size;
    size_t extra=(required-blocks.size())*block_size;
    if(extra>available) return false;
    while(blocks.size()<required) blocks.emplace_back(new uint8_t[block_size]);
    while(count) {
        size_t offset=length%block_size,n=std::min(count,block_size-offset);
        memcpy(blocks[length/block_size].get()+offset,bytes,n); length+=n; bytes+=n; count-=n;
    }
    return true;
}
std::vector<uint8_t> CompressedBuffer::Flatten() {
    std::vector<uint8_t> output(length); size_t p=0;
    for(const auto& block:blocks) { size_t n=std::min(block_size,length-p); memcpy(output.data()+p,block.get(),n); p+=n; }
    blocks.clear(); length=0; return output;
}
}
