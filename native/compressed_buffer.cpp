#include "compressed_buffer.h"
#include <algorithm>
#include <cstring>
#include <limits>
namespace history {
bool CompressedBuffer::Append(const uint8_t* bytes,size_t count,size_t available) {
    if(count && !bytes) return false;
    if(count>std::numeric_limits<size_t>::max()-length-block_size) return false;
    size_t required=(length+count+block_size-1)/block_size;
    size_t extra=required>blocks.size()?(required-blocks.size())*block_size:0;
    if(extra>available) return false;
    while(blocks.size()<required) blocks.push_back(std::make_unique<uint8_t[]>(block_size));
    while(count) {
        size_t offset=length%block_size,n=std::min(count,block_size-offset);
        memcpy(blocks[length/block_size].get()+offset,bytes,n); length+=n; bytes+=n; count-=n;
    }
    return true;
}
bool CompressedBuffer::RetainPrefixAndSuffix(size_t prefix,size_t suffix) noexcept {
    if(prefix>suffix||suffix>length)return false;
    const auto tail=length-suffix;
    if(prefix>std::numeric_limits<size_t>::max()-tail)return false;
    size_t copied=0;
    while(copied<tail) {
        const auto source=suffix+copied,destination=prefix+copied;
        const auto amount=std::min({tail-copied,block_size-source%block_size,
                                    block_size-destination%block_size});
        std::memmove(blocks[destination/block_size].get()+destination%block_size,
                     blocks[source/block_size].get()+source%block_size,amount);
        copied+=amount;
    }
    length=prefix+tail;
    const auto required=(length+block_size-1)/block_size;
    while(blocks.size()>required)blocks.pop_back();
    return true;
}
bool CompressedBuffer::CopyRange(size_t destination,size_t source,size_t count) noexcept {
    if(destination>source||source>length||count>length-source)return false;
    while(count) {
        const auto n=std::min({count,block_size-source%block_size,block_size-destination%block_size});
        std::memmove(blocks[destination/block_size].get()+destination%block_size,
                     blocks[source/block_size].get()+source%block_size,n);
        source+=n;destination+=n;count-=n;
    }
    return true;
}
bool CompressedBuffer::SplitSuffix(size_t offset,CompressedBuffer& suffix,size_t available) {
    if(offset>length||&suffix==this||suffix.size())return false;
    CompressedBuffer result;
    for(size_t at=offset;at<length;) {
        const auto amount=std::min(length-at,block_size-at%block_size);
        if(!result.Append(blocks[at/block_size].get()+at%block_size,amount,
                          available>=result.capacity()?available-result.capacity():0))return false;
        at+=amount;
    }
    RetainPrefixAndSuffix(offset,length);suffix=std::move(result);return true;
}
std::vector<uint8_t> CompressedBuffer::Flatten() {
    std::vector<uint8_t> output(length); size_t p=0;
    for(const auto& block:blocks) { size_t n=std::min(block_size,length-p); memcpy(output.data()+p,block.get(),n); p+=n; }
    blocks.clear(); length=0; return output;
}
}
