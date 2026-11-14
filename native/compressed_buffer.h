#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
namespace history {
// Fixed blocks avoid reallocating (and briefly duplicating) growing tracks.
class CompressedBuffer {
    static constexpr size_t block_size=65536;
    std::vector<std::unique_ptr<uint8_t[]>> blocks;
    size_t length=0;
public:
    size_t size() const { return length; }
    size_t capacity() const { return blocks.size()*block_size; }
    uint8_t operator[](size_t p) const { return blocks[p/block_size][p%block_size]; }
    bool Append(const uint8_t* bytes,size_t count,size_t available);
    // Compact [suffix,size) directly after [0,prefix) without allocating.
    // Used when a decoder restarts a logical stream on the same context.
    bool RetainPrefixAndSuffix(size_t prefix,size_t suffix) noexcept;
    // Move a retained forward range toward the beginning, without allocation.
    bool CopyRange(size_t destination,size_t source,size_t count) noexcept;
    // Only compile a contiguous source after a complete listen; release blocks
    // before creating the tagged output. At most one additional full buffer.
    bool SplitSuffix(size_t offset,CompressedBuffer& suffix,size_t available);
    std::vector<uint8_t> Flatten();
};
}
