#pragma once
#include <atomic>
#include <cstdint>
namespace history {
// RAM-only wake-up hint shared by cached text and downloaded artwork. Polling
// compares one number, never copies lyrics or image bytes to build a queue key.
inline std::atomic<std::uint64_t> metadata_revision{0};
inline void MetadataChanged() noexcept {metadata_revision.fetch_add(1,std::memory_order_release);}
inline std::uint64_t MetadataRevision() noexcept {return metadata_revision.load(std::memory_order_acquire);}
}
