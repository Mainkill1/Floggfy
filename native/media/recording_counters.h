#pragma once
#include <atomic>
#include <cstdint>
#include <utility>
namespace history {
enum class RecordKind {Stream,Metadata};
struct RecordingCounterValues {
    std::uint64_t stream_push=0,stream_pop=0,metadata_push=0,metadata_pop=0;
    std::uint64_t matched=0,saved=0,skipped=0,unmatched=0;
    std::uint64_t StreamPending() const noexcept{return stream_push>stream_pop?stream_push-stream_pop:0;}
    std::uint64_t MetadataPending() const noexcept{return metadata_push>metadata_pop?metadata_push-metadata_pop:0;}
    bool operator==(const RecordingCounterValues& other) const noexcept {
        return stream_push==other.stream_push&&stream_pop==other.stream_pop&&
            metadata_push==other.metadata_push&&metadata_pop==other.metadata_pop&&
            matched==other.matched&&saved==other.saved&&skipped==other.skipped&&unmatched==other.unmatched;
    }
};
struct RecordingCounters {
    std::atomic<std::uint64_t> stream_push{0},stream_pop{0},metadata_push{0},metadata_pop{0};
    std::atomic<std::uint64_t> matched{0},saved{0},skipped{0},unmatched{0};
    RecordingCounterValues Snapshot() const noexcept {
        return {stream_push.load(std::memory_order_relaxed),stream_pop.load(std::memory_order_relaxed),
            metadata_push.load(std::memory_order_relaxed),metadata_pop.load(std::memory_order_relaxed),
            matched.load(std::memory_order_relaxed),saved.load(std::memory_order_relaxed),
            skipped.load(std::memory_order_relaxed),unmatched.load(std::memory_order_relaxed)};
    }
};
inline RecordingCounters recording_counters;
// Diagnostic ownership follows the retained record through queue moves/retries.
// These counts never authorize metadata association or publication.
class RecordCounter {
    RecordKind kind_=RecordKind::Stream;
    bool active_=false;
public:
    RecordCounter() noexcept=default;
    RecordCounter(const RecordCounter&)=delete;
    RecordCounter& operator=(const RecordCounter&)=delete;
    RecordCounter(RecordCounter&& other) noexcept:
        kind_(other.kind_),active_(std::exchange(other.active_,false)) {}
    RecordCounter& operator=(RecordCounter&& other) noexcept {
        if(this!=&other){Reset();kind_=other.kind_;active_=std::exchange(other.active_,false);}
        return *this;
    }
    ~RecordCounter(){Reset();}
    bool Active() const noexcept{return active_;}
    void Reset() noexcept {
        if(!active_)return;
        active_=false;
        (kind_==RecordKind::Stream?recording_counters.stream_pop:recording_counters.metadata_pop)
            .fetch_add(1,std::memory_order_relaxed);
    }
    void Begin(RecordKind kind) noexcept {
        Reset();kind_=kind;active_=true;
        (kind_==RecordKind::Stream?recording_counters.stream_push:recording_counters.metadata_push)
            .fetch_add(1,std::memory_order_relaxed);
    }
};
}
