#include "../native/matcher_diagnostics.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <limits>

int main() {
    using namespace history;
    MatcherSnapshot snapshot;snapshot.inventory_evaluated=true;snapshot.snapshot_id=1;snapshot.capture_id=7;
    auto* candidate=snapshot.AddCandidate(12);assert(candidate);
    candidate->usable=true;candidate->source_duration_ms=0;
    candidate->first_seen_delta_ms=-1200;candidate->start_delta_ms=0;
    snapshot.RecordDecision(12,MatcherReason::OutsideStartWindow,true,false);
    auto serialized=SerializeMatcherSnapshot(snapshot);
    assert(!serialized.records.empty()&&serialized.bytes<=16*1024);
    std::string joined;
    for(const auto& part:serialized.records){assert(part.size()<768);joined+=part;}
    assert(joined.find("metadata=12")!=std::string::npos);
    assert(joined.find("source_duration_ms=0")!=std::string::npos);
    assert(joined.find("duration_delta_ms=unknown")!=std::string::npos);
    assert(joined.find("end_delta_ms=unknown")!=std::string::npos);
    assert(joined.find("timing_evaluated=1 duration_evaluated=0")!=std::string::npos);
    assert(joined.find("decision=outside-start-window")!=std::string::npos);
    const auto signature=MatcherSnapshotSignature(snapshot);
    snapshot.source_bytes=65536;
    assert(MatcherSnapshotSignature(snapshot)==signature&&"byte arrival alone is not a changed matching decision");
    snapshot.snapshot_id=999;snapshot.FinalizeSnapshot(MatcherReason::SavedUnmatched);
    assert(MatcherSnapshotSignature(snapshot)==signature);
    snapshot.RecordDecision(12,MatcherReason::DurationMismatch,true,true);
    assert(MatcherSnapshotSignature(snapshot)==signature);
    assert(snapshot.metadata[0].decision==MatcherReason::OutsideStartWindow&&!snapshot.metadata[0].duration_evaluated);
    snapshot.RecordDecision(12,MatcherReason::MatchedNative,true,true);
    assert(MatcherSnapshotSignature(snapshot)!=signature);
    for(uint64_t id=13;id<52;++id)snapshot.AddCandidate(id);
    for(uint64_t id=100;id<120;++id)snapshot.AddCompetitor(id);
    serialized=SerializeMatcherSnapshot(snapshot);
    assert(snapshot.metadata_total==40&&snapshot.competitor_total==20);
    assert(snapshot.metadata_count==32&&snapshot.competitor_count==16);
    joined.clear();for(const auto& part:serialized.records){assert(part.size()<768);joined+=part;}
    assert(joined.find("metadata_omitted=8")!=std::string::npos);
    assert(joined.find("competitors_omitted=4")!=std::string::npos);
    assert(joined.find("part=1/")!=std::string::npos&&joined.find("kind=summary")!=std::string::npos);
    assert(joined.find("spotify:")==std::string::npos&&joined.find("0x")==std::string::npos);
    assert(joined.find("title=")==std::string::npos&&joined.find("path=/")==std::string::npos&&joined.find("path=C:")==std::string::npos);
    const auto capped=SerializeMatcherSnapshot(snapshot,1024);
    assert(capped.bytes<=1024&&capped.truncated&&capped.omitted_records>0);
    assert(capped.records.front().find("truncated=1")!=std::string::npos);
    assert(capped.records[1].find("matcher_terminal reason=saved-unmatched")!=std::string::npos);
    const auto full_signature=MatcherSnapshotSignature(snapshot);
    snapshot.metadata[31].decision=MatcherReason::WaitingDuration;
    assert(MatcherSnapshotSignature(snapshot)!=full_signature);
    for(std::size_t i=0;i<snapshot.metadata_count;++i) {
        auto& item=snapshot.metadata[i];
        item.metadata_id=std::numeric_limits<uint64_t>::max()-i;
        item.first_seen_delta_ms=std::numeric_limits<double>::max();
        item.start_delta_ms=item.first_seen_delta_ms;item.end_delta_ms=item.first_seen_delta_ms;
        item.duration_delta_ms=item.first_seen_delta_ms;item.source_duration_ms=item.first_seen_delta_ms;
        item.metadata_duration_ms=item.first_seen_delta_ms;
    }
    snapshot.snapshot_id=std::numeric_limits<uint64_t>::max();snapshot.capture_id=snapshot.snapshot_id;
    snapshot.epoch=snapshot.snapshot_id;snapshot.metadata_total=snapshot.snapshot_id;
    snapshot.competitor_total=snapshot.snapshot_id;snapshot.queue_dropped=snapshot.snapshot_id;
    snapshot.ownership_state=std::numeric_limits<unsigned>::max();snapshot.ownership_reason=snapshot.ownership_state;
    snapshot.source_duration_ms=std::numeric_limits<double>::max();
    snapshot.end_ms=std::numeric_limits<double>::quiet_NaN();
    const auto worst=SerializeMatcherSnapshot(snapshot);
    assert(worst.truncated&&worst.bytes<=16*1024);
    for(const auto& part:worst.records){assert(part.size()<768);assert(part.find("nan")==std::string::npos);}
    const auto omitted_signature=MatcherSnapshotSignature(snapshot);
    snapshot.competitors[15].complete=true;
    assert(MatcherSnapshotSignature(snapshot)!=omitted_signature);
    const auto unassociated_signature=MatcherSnapshotSignature(snapshot);
    snapshot.association_metadata_id=70;
    assert(MatcherSnapshotSignature(snapshot)!=unassociated_signature);
    const auto associated=SerializeMatcherSnapshot(snapshot);
    bool association_found=false;
    for(const auto& part:associated.records)if(part.find("matcher_association")!=std::string::npos) {
        association_found=true;assert(part.find("metadata=70 retained=0")!=std::string::npos);
    }
    assert(association_found);
    const auto inherited_signature=MatcherSnapshotSignature(snapshot);
    snapshot.association_retained=true;
    assert(MatcherSnapshotSignature(snapshot)!=inherited_signature);
    MatcherSnapshot early;early.capture_id=1;early.abort_reason=3;
    early.FinalizeSnapshot(MatcherReason::CaptureAborted);
    const auto early_serialized=SerializeMatcherSnapshot(early);
    assert(early_serialized.records[0].find("metadata_total=unknown")!=std::string::npos);
    assert(early_serialized.records[0].find("inventory_evaluated=0")!=std::string::npos);
    assert(early_serialized.records[0].find("abort_reason=3")!=std::string::npos);
    assert(early_serialized.records[1].find("reason=capture-aborted")!=std::string::npos);
    MatcherDiagnosticBudget budget(1024);
    assert(budget.Admit(1000,3).admitted);
    auto exhausted=budget.Admit(25,2);assert(!exhausted.admitted&&exhausted.emit_exhaustion);
    assert(exhausted.omitted_snapshots==1&&exhausted.omitted_records==2);
    exhausted=budget.Admit(25,4);assert(!exhausted.admitted&&!exhausted.emit_exhaustion);
    assert(exhausted.omitted_snapshots==2&&exhausted.omitted_records==6&&budget.Used()==1000);
    assert(budget.Admit(24,1).admitted&&budget.Used()==1024);
    MatcherDiagnosticBudget session;
    assert(session.Admit(2*1024*1024,1).admitted);
    assert(!session.Admit(1,1).admitted);
    std::puts("PASS: bounded matcher snapshots, coverage, unknown fields, privacy, signature and session budget");
}
