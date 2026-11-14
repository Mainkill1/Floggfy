#include "../native/matcher_diagnostics.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <limits>

void SnapshotDeltas() {
    using namespace history;
    MatcherSnapshot snapshot;snapshot.snapshot_id=1;snapshot.capture_id=7;snapshot.inventory_evaluated=true;
    for(uint64_t id=1;id<=6;++id) {
        auto* candidate=snapshot.AddCandidate(id);candidate->usable=true;
        candidate->decision=MatcherReason::OutsideStartWindow;candidate->start_delta_ms=20000;
    }
    snapshot.AddCompetitor(8);
    MatcherSnapshotEmissionState emission;
    auto full=SerializeMatcherSnapshot(snapshot,emission);
    assert(full.records.size()==9&&!full.truncated&&emission.snapshot_id==1);
    assert(full.records.front().find("mode=full")!=std::string::npos);
    snapshot.snapshot_id=2;snapshot.competitors[0].complete=true;
    auto delta=SerializeMatcherSnapshot(snapshot,emission);
    assert(delta.records.size()==2&&delta.bytes<full.bytes/2&&
        "one competitor change must not repeat the unchanged six candidate explanations");
    assert(delta.records[0].find("mode=delta base_snapshot=1")!=std::string::npos);
    assert(delta.records[1].find("matcher_competitor")!=std::string::npos&&
        delta.records[1].find("complete=1")!=std::string::npos);
    snapshot.snapshot_id=3;snapshot.metadata[5].decision=MatcherReason::DurationMismatch;
    delta=SerializeMatcherSnapshot(snapshot,emission);
    assert(delta.records.size()==2&&delta.records[1].find("metadata=6 decision=duration-mismatch")!=std::string::npos);
    snapshot.snapshot_id=4;--snapshot.metadata_count;--snapshot.metadata_total;
    snapshot.competitor_count=0;snapshot.competitor_total=0;
    delta=SerializeMatcherSnapshot(snapshot,emission);
    assert(delta.records.size()==3&&delta.records[1].find("matcher_candidate_removed")!=std::string::npos&&
        delta.records[1].find("metadata=6")!=std::string::npos&&delta.records[2].find("matcher_competitor_removed")!=std::string::npos);
    snapshot.snapshot_id=5;snapshot.FinalizeSnapshot(MatcherReason::RetiredInvalid);
    auto terminal=SerializeMatcherSnapshot(snapshot,emission);
    assert(terminal.records.size()==8&&terminal.records[0].find("mode=full")!=std::string::npos&&
        terminal.records[1].find("matcher_terminal reason=retired-invalid")!=std::string::npos);
    for(std::size_t i=3;i<terminal.records.size();++i)
        assert(terminal.records[i].find("decision=outside-start-window")!=std::string::npos);
    snapshot.snapshot_id=6;snapshot.FinalizeSnapshot(MatcherReason::NotEvaluated);
    snapshot.duration_tolerance_ms=100;
    delta=SerializeMatcherSnapshot(snapshot,emission);
    assert(delta.records.size()==2&&delta.records[1].find("duration_tolerance_ms=100")!=std::string::npos);
    // A caller stages cache changes until budget admission. A rejected delta
    // must not become the base of the next admitted snapshot.
    auto proposed=emission;snapshot.snapshot_id=7;snapshot.duration_tolerance_ms=200;
    const auto rejected=SerializeMatcherSnapshot(snapshot,proposed);
    MatcherDiagnosticBudget empty_budget(0);
    assert(!empty_budget.Admit(rejected.bytes,rejected.records.size()).admitted);
    snapshot.snapshot_id=8;snapshot.duration_tolerance_ms=300;
    delta=SerializeMatcherSnapshot(snapshot,emission);
    assert(delta.records[0].find("base_snapshot=6")!=std::string::npos);
    snapshot.snapshot_id=9;snapshot.FinalizeSnapshot(MatcherReason::RetiredInvalid);
    const auto truncated=SerializeMatcherSnapshot(snapshot,emission,1024);
    assert(truncated.truncated&&emission.snapshot_id==0);
    snapshot.snapshot_id=10;snapshot.FinalizeSnapshot(MatcherReason::NotEvaluated);
    const auto recovery=SerializeMatcherSnapshot(snapshot,emission);
    assert(recovery.records[0].find("mode=full")!=std::string::npos&&recovery.records.size()==7);
}

int main() {
    using namespace history;
    SnapshotDeltas();
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
    snapshot.end_ms=100000;
    assert(MatcherSnapshotSignature(snapshot)==signature&&
        "absolute observation time alone must not repeat unchanged matching evidence");
    snapshot.metadata[0].end_delta_ms=250;
    assert(MatcherSnapshotSignature(snapshot)!=signature&&
        "changed relative timing evidence must still emit a snapshot");
    snapshot.metadata[0].end_delta_ms.reset();
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
