#include "compressed_buffer.h"
#include <algorithm>
#include <cstring>
#include <limits>
namespace history {
CompressedBuffer::BlockView CompressedBuffer::BlockAt(size_t offset) const noexcept {
    if(offset>=length)return {nullptr,0};
    const size_t within=offset%block_size;
    return {blocks[offset/block_size].get()+within,std::min(block_size-within,length-offset)};
}
const uint8_t* CompressedBuffer::ReadContiguous(size_t offset,size_t count,std::vector<uint8_t>& scratch) const {
    if(!count || offset>=length || count>length-offset)return nullptr;
    auto block=BlockAt(offset);
    if(count<=block.size)return block.data;
    scratch.clear();
    scratch.reserve(count);
    for(;;) {
        const size_t copied=std::min(count-scratch.size(),block.size);
        scratch.insert(scratch.end(),block.data,block.data+copied);
        if(scratch.size()==count)return scratch.data();
        block=BlockAt(offset+scratch.size());
    }
}
bool CompressedBuffer::Append(const uint8_t* bytes,size_t count,size_t available) {
    if(count && !bytes) return false;
    if(count>std::numeric_limits<size_t>::max()-length-block_size) return false;
    size_t required=(length+count+block_size-1)/block_size;
    size_t extra=required>blocks.size()?(required-blocks.size())*block_size:0;
    if(extra>available) return false;
    // Append initializes each byte before advancing length.
    while(blocks.size()<required)blocks.push_back(std::unique_ptr<uint8_t[]>(new uint8_t[block_size]));
    while(count) {
        size_t offset=length%block_size,n=std::min(count,block_size-offset);
        memcpy(blocks[length/block_size].get()+offset,bytes,n); length+=n; bytes+=n; count-=n;
    }
    return true;
}
bool CompressedBuffer::RetainPrefixAndSuffix(size_t prefix,size_t suffix) noexcept {
    if(suffix>length || !CopyRange(prefix,suffix,length-suffix))return false;
    length=prefix+(length-suffix);
    const auto required=(length+block_size-1)/block_size;
    while(blocks.size()>required)blocks.pop_back();
    return true;
}
bool CompressedBuffer::CopyRange(size_t destination,size_t source,size_t count) noexcept {
    if(destination>source||source>length||count>length-source)return false;
    if(destination==source)return true;
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
    std::vector<uint8_t> output;
    output.reserve(length);
    while(output.size()<length) {
        const auto block=BlockAt(output.size());
        output.insert(output.end(),block.data,block.data+block.size);
    }
    blocks.clear(); length=0; return output;
}
}
