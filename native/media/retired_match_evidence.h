#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
namespace history {
// Audio is disposable after retirement; unresolved competitors are not. Keep
// bounded facts, with conservative coverage when the ring overwrites an entry.
class RetiredMatchEvidence {
public:
    struct Entry {std::uint64_t id=0;double start=0,duration=0;};
private:
    struct Ring {
        std::array<Entry,32> entries{};
        unsigned count=0,next=0;
        double overflow_through=-std::numeric_limits<double>::infinity();
        void Remember(Entry entry) noexcept {
            for(unsigned i=0;i<count;++i)if(entries[i].id==entry.id&&entries[i].start==entry.start) {
                if(entry.duration>0)entries[i].duration=entry.duration;
                return;
            }
            if(count==entries.size())overflow_through=std::max(overflow_through,entries[next].start);
            else ++count;
            entries[next]=entry;next=(next+1)%entries.size();
        }
        bool Blocks(double start,double duration,unsigned offset_ms,unsigned duration_ms) const noexcept {
            if(!offset_ms)return false;
            const auto window=offset_ms/1000.0;
            if(start<=overflow_through+window)return true;
            for(unsigned i=0;i<count;++i) {
                const auto& old=entries[i];
                if(std::fabs(start-old.start)<=window&&
                   (!duration||!old.duration||std::fabs(duration-old.duration)<=duration_ms/1000.0+1e-6))return true;
            }
            return false;
        }
    } captures_,metadata_;
public:
    void RememberCapture(Entry entry) noexcept {captures_.Remember(entry);}
    void RememberMetadata(Entry entry) noexcept {metadata_.Remember(entry);}
    bool BlocksMetadata(double start,double duration,unsigned offset_ms,unsigned duration_ms) const noexcept {
        return captures_.Blocks(start,duration,offset_ms,duration_ms);
    }
    bool BlocksCapture(double start,double duration,unsigned offset_ms,unsigned duration_ms) const noexcept {
        return metadata_.Blocks(start,duration,offset_ms,duration_ms);
    }
};
}
