#include "../native/recording_counters.h"
#include "../native/bounded_queue.h"
#include <cassert>
#include <utility>
using namespace history;
int main() {
    {
        RecordCounter empty;
        assert(!empty.Active());
        RecordCounter source;source.Begin(RecordKind::Stream);
        RecordCounter moved(std::move(source));
        assert(!source.Active()&&moved.Active());
        auto counts=recording_counters.Snapshot();
        assert(counts.stream_push==1&&counts.stream_pop==0);
        RecordCounter replaced;replaced.Begin(RecordKind::Metadata);
        replaced=std::move(moved);
        counts=recording_counters.Snapshot();
        assert(!moved.Active()&&replaced.Active()&&counts.stream_push==1&&
               counts.stream_pop==0&&counts.metadata_push==1&&counts.metadata_pop==1);
        replaced.Begin(RecordKind::Stream);
        counts=recording_counters.Snapshot();
        assert(counts.stream_push==2&&counts.stream_pop==1);
    }
    auto counts=recording_counters.Snapshot();
    assert(counts.stream_push==2&&counts.stream_pop==2&&
           counts.metadata_push==1&&counts.metadata_pop==1);
    {
        BoundedQueue<RecordCounter,1> pending;
        RecordCounter first;first.Begin(RecordKind::Metadata);
        assert(pending.Push(std::move(first))&&!first.Active());
        RecordCounter retry;retry.Begin(RecordKind::Metadata);
        assert(!pending.Push(std::move(retry))&&retry.Active());
        counts=recording_counters.Snapshot();
        assert(counts.metadata_push==3&&counts.metadata_pop==1);
        RecordCounter saved;assert(pending.Pop(saved)&&saved.Active());
        assert(pending.Push(std::move(retry))&&!retry.Active());
        counts=recording_counters.Snapshot();
        assert(counts.metadata_push==3&&counts.metadata_pop==1&&counts.MetadataPending()==2);
        saved.Reset();
        counts=recording_counters.Snapshot();
        assert(counts.metadata_pop==2&&counts.MetadataPending()==1);
    }
    counts=recording_counters.Snapshot();
    assert(counts.StreamPending()==0&&counts.MetadataPending()==0&&
           counts.metadata_push==3&&counts.metadata_pop==3);
}
