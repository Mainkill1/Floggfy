#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>
#include "library_layout.h"
namespace history {
inline constexpr std::size_t kMatcherSnapshotBytes=16*1024;
inline constexpr std::size_t kMatcherSessionBytes=2*1024*1024;
enum class MatcherReason {
    NotEvaluated,Pending,MetadataAssigned,MetadataUnusable,OutsideStartWindow,
    DurationMismatch,WaitingNativeIdentity,RetiredStreamEvidence,RetiredMetadataEvidence,
    AmbiguousStreams,AmbiguousMetadata,WaitingDuration,OwnershipExcluded,BindingPending,
    MatchedNative,MatchedWeak,RetiredCompleteUnmatched,SavedUnmatched,SaveUnmatchedDisabled,RetiredInvalid,CaptureAborted
};
inline const char* MatcherReasonName(MatcherReason reason) noexcept {
    switch(reason) {
#define FLOGGFY_MATCHER_REASON(value,name) case MatcherReason::value:return name
        FLOGGFY_MATCHER_REASON(NotEvaluated,"not-evaluated");
        FLOGGFY_MATCHER_REASON(Pending,"pending");
        FLOGGFY_MATCHER_REASON(MetadataAssigned,"metadata-assigned");
        FLOGGFY_MATCHER_REASON(MetadataUnusable,"metadata-unusable");
        FLOGGFY_MATCHER_REASON(OutsideStartWindow,"outside-start-window");
        FLOGGFY_MATCHER_REASON(DurationMismatch,"duration-mismatch");
        FLOGGFY_MATCHER_REASON(WaitingNativeIdentity,"waiting-native-identity");
        FLOGGFY_MATCHER_REASON(RetiredStreamEvidence,"retired-stream-evidence");
        FLOGGFY_MATCHER_REASON(RetiredMetadataEvidence,"retired-metadata-evidence");
        FLOGGFY_MATCHER_REASON(AmbiguousStreams,"ambiguous-streams");
        FLOGGFY_MATCHER_REASON(AmbiguousMetadata,"ambiguous-metadata");
        FLOGGFY_MATCHER_REASON(WaitingDuration,"waiting-duration");
        FLOGGFY_MATCHER_REASON(OwnershipExcluded,"ownership-excluded");
        FLOGGFY_MATCHER_REASON(BindingPending,"binding-pending");
        FLOGGFY_MATCHER_REASON(MatchedNative,"matched-native-id");
        FLOGGFY_MATCHER_REASON(MatchedWeak,"matched-unique-evidence");
        FLOGGFY_MATCHER_REASON(RetiredCompleteUnmatched,"retired-complete-unmatched");
        FLOGGFY_MATCHER_REASON(SavedUnmatched,"saved-unmatched");
        FLOGGFY_MATCHER_REASON(SaveUnmatchedDisabled,"save-unmatched-disabled");
        FLOGGFY_MATCHER_REASON(RetiredInvalid,"retired-invalid");
        FLOGGFY_MATCHER_REASON(CaptureAborted,"capture-aborted");
#undef FLOGGFY_MATCHER_REASON
    }
    return "unknown";
}
enum class MatcherPath {Unknown,Native,Weak};
inline const char* MatcherPathName(MatcherPath path) noexcept {
    return path==MatcherPath::Native?"native":path==MatcherPath::Weak?"weak":"unknown";
}
enum class MatcherPriorHistory {Unknown,NotRetained,Retained};
struct MatcherCandidate {
    uint64_t metadata_id=0;
    bool assigned=false,usable=false,timing_evaluated=false,duration_evaluated=false;
    MatcherReason decision=MatcherReason::NotEvaluated;
    MatcherPath path=MatcherPath::Unknown;
    std::optional<unsigned> ownership_state,ownership_reason;
    std::optional<double> first_seen_delta_ms,start_delta_ms,end_delta_ms,duration_delta_ms;
    std::optional<double> source_duration_ms,metadata_duration_ms;
};
struct MatcherCompetitor {
    uint64_t capture_id=0;
    bool complete=false,associated=false;
    std::optional<double> duration_ms;
};
// Value snapshot of facts already read by the production matcher. This helper
// never looks up identities, reruns eligibility checks or owns matching state.
// IDs must be session-local counters. No user strings are accepted or emitted.
struct MatcherSnapshot {
    std::optional<Codec> codec;
    uint64_t snapshot_id=0,capture_id=0,epoch=0;
    MatcherPath path=MatcherPath::Unknown;
    std::optional<unsigned> ownership_state,ownership_reason;
    std::optional<uint64_t> association_metadata_id;
    bool association_retained=false;
    std::optional<uint64_t> queue_dropped;
    std::optional<double> source_duration_ms,end_ms;
    MatcherReason terminal=MatcherReason::NotEvaluated;
    bool complete=false,inventory_evaluated=false;
    std::optional<unsigned> abort_reason;
    uint64_t source_bytes=0,subsequent_starts=0,retirement_threshold=0;
    unsigned time_offset_tolerance_ms=0,duration_tolerance_ms=0;
    std::optional<uint64_t> known_pruned_metadata;
    MatcherPriorHistory prior_history=MatcherPriorHistory::Unknown;
    uint64_t metadata_total=0,competitor_total=0;
    std::size_t metadata_count=0,competitor_count=0;
    std::array<MatcherCandidate,32> metadata{};
    std::array<MatcherCompetitor,16> competitors{};
    MatcherCandidate* AddCandidate(uint64_t id) noexcept {
        ++metadata_total;if(metadata_count==metadata.size())return nullptr;
        auto& item=metadata[metadata_count++];item.metadata_id=id;return &item;
    }
    MatcherCompetitor* AddCompetitor(uint64_t id) noexcept {
        ++competitor_total;if(competitor_count==competitors.size())return nullptr;
        auto& item=competitors[competitor_count++];item.capture_id=id;return &item;
    }
    void RecordDecision(uint64_t id,MatcherReason reason,bool timing=false,bool duration=false) noexcept {
        for(std::size_t i=0;i<metadata_count;++i)if(metadata[i].metadata_id==id) {
            auto& item=metadata[i];
            if(item.decision!=MatcherReason::NotEvaluated&&item.decision!=MatcherReason::Pending&&
               reason!=MatcherReason::MatchedNative&&reason!=MatcherReason::MatchedWeak)return;
            item.decision=reason;item.timing_evaluated=timing;
            item.duration_evaluated=duration;return;
        }
    }
    void FinalizeSnapshot(MatcherReason reason) noexcept {terminal=reason;}
};
struct SerializedMatcherSnapshot {
    std::vector<std::string> records;
    std::size_t bytes=0,omitted_records=0;
    bool truncated=false;
};
// Per-capture cache of bounded row fingerprints, never serialized log strings.
// Only advance this state after admitting the snapshot to the diagnostic budget.
struct MatcherSnapshotEmissionState {
    enum class Kind {Settings,Association,Candidate,Competitor};
    struct Row {Kind kind=Kind::Settings;uint64_t id=0,signature=0;};
    uint64_t snapshot_id=0;
    std::size_t count=0;
    std::array<Row,50> rows{};
};
namespace matcher_diagnostic_detail {
inline std::string Number(std::optional<double> value) {
    if(!value||!std::isfinite(*value))return "unknown";
    char number[32];std::snprintf(number,sizeof(number),"%.9g",*value);return number;
}
template<class Integer> inline std::string IntegerNumber(std::optional<Integer> value) {
    return value?std::to_string(*value):"unknown";
}
inline std::string Common(const MatcherSnapshot& snapshot) {
    return " snapshot="+std::to_string(snapshot.snapshot_id)+
        " capture="+std::to_string(snapshot.capture_id)+" epoch="+std::to_string(snapshot.epoch);
}
inline SerializedMatcherSnapshot Serialize(const MatcherSnapshot& snapshot,std::size_t limit,
                                           MatcherSnapshotEmissionState* emission=nullptr) {
    SerializedMatcherSnapshot result;limit=std::min(limit,kMatcherSnapshotBytes);
    std::vector<std::string> bodies;
    const auto common=Common(snapshot);
    using Kind=MatcherSnapshotEmissionState::Kind;
    MatcherSnapshotEmissionState next;next.snapshot_id=snapshot.snapshot_id;
    const bool delta=emission&&emission->snapshot_id&&snapshot.terminal==MatcherReason::NotEvaluated;
    const auto add=[&](Kind kind,uint64_t id,std::string body) {
        uint64_t signature=14695981039346656037ull;
        // The common sequence/capture prefix is correlation, not row evidence.
        const auto at=body.find(common);
        for(std::size_t i=0;i<body.size();++i) {
            if(i==at){i+=common.size()-1;continue;}
            signature^=static_cast<unsigned char>(body[i]);signature*=1099511628211ull;
        }
        next.rows[next.count++]={kind,id,signature};
        if(delta)for(std::size_t i=0;i<emission->count;++i) {
            const auto& prior=emission->rows[i];
            if(prior.kind==kind&&prior.id==id&&prior.signature==signature)return;
        }
        bodies.push_back(std::move(body));
    };
    // Terminal evidence has priority over a long candidate inventory.
    if(snapshot.terminal!=MatcherReason::NotEvaluated)
        bodies.push_back(std::string("matcher_terminal reason=")+MatcherReasonName(snapshot.terminal)+common);
    if(snapshot.association_metadata_id)
        add(Kind::Association,0,"matcher_association"+common+" metadata="+std::to_string(*snapshot.association_metadata_id)+
            " retained="+std::to_string(snapshot.association_retained)+" path="+MatcherPathName(snapshot.path));
    add(Kind::Settings,0,"matcher_settings"+common+" complete="+std::to_string(snapshot.complete)+
        " source_bytes="+std::to_string(snapshot.source_bytes)+" subsequent_starts="+std::to_string(snapshot.subsequent_starts)+
        " retirement_threshold="+std::to_string(snapshot.retirement_threshold)+
        " timing_tolerance_ms="+std::to_string(snapshot.time_offset_tolerance_ms)+
        " duration_tolerance_ms="+std::to_string(snapshot.duration_tolerance_ms)+
        " known_pruned_metadata="+IntegerNumber(snapshot.known_pruned_metadata)+
        " prior_history="+(snapshot.prior_history==MatcherPriorHistory::NotRetained?"not-retained":
                               snapshot.prior_history==MatcherPriorHistory::Retained?"retained":"unknown"));
    for(std::size_t i=0;i<snapshot.metadata_count;++i) {
        const auto& item=snapshot.metadata[i];
        add(Kind::Candidate,item.metadata_id,"matcher_candidate"+common+" metadata="+std::to_string(item.metadata_id)+
            " decision="+MatcherReasonName(item.decision)+" path="+MatcherPathName(item.path)+
            " assigned="+std::to_string(item.assigned)+" usable="+std::to_string(item.usable)+
            " timing_evaluated="+std::to_string(item.timing_evaluated)+
            " duration_evaluated="+std::to_string(item.duration_evaluated)+
            " first_seen_delta_ms="+Number(item.first_seen_delta_ms)+" start_delta_ms="+Number(item.start_delta_ms)+
            " end_delta_ms="+Number(item.end_delta_ms)+" duration_delta_ms="+Number(item.duration_delta_ms)+
            " source_duration_ms="+Number(item.source_duration_ms)+" metadata_duration_ms="+Number(item.metadata_duration_ms)+
            " ownership_state="+IntegerNumber(item.ownership_state)+" ownership_reason="+IntegerNumber(item.ownership_reason));
    }
    for(std::size_t i=0;i<snapshot.competitor_count;++i) {
        const auto& item=snapshot.competitors[i];
        add(Kind::Competitor,item.capture_id,"matcher_competitor"+common+" competitor="+std::to_string(item.capture_id)+
            " complete="+std::to_string(item.complete)+" associated="+std::to_string(item.associated)+
            " duration_ms="+Number(item.duration_ms));
    }
    if(delta)for(std::size_t i=0;i<emission->count;++i) {
        const auto& prior=emission->rows[i];
        const auto found=std::find_if(next.rows.begin(),next.rows.begin()+next.count,[&](const auto& row) {
            return row.kind==prior.kind&&row.id==prior.id;
        });
        if(found!=next.rows.begin()+next.count)continue;
        if(prior.kind==Kind::Candidate)bodies.push_back("matcher_candidate_removed"+common+" metadata="+std::to_string(prior.id));
        else if(prior.kind==Kind::Competitor)bodies.push_back("matcher_competitor_removed"+common+" competitor="+std::to_string(prior.id));
        else if(prior.kind==Kind::Association)bodies.push_back("matcher_association_removed"+common);
    }
    // Reserve a bounded summary before accepting candidate parts. All strings
    // come from fixed tokens/numbers; one logical candidate always fits a line.
    constexpr std::size_t summary_reserve=640,part_reserve=24;
    std::size_t selected=0,body_bytes=0;
    if(limit>=summary_reserve)for(const auto& body:bodies) {
        if(body.size()+part_reserve>=768||body_bytes+body.size()+part_reserve>limit-summary_reserve)break;
        body_bytes+=body.size()+part_reserve;++selected;
    }
    result.omitted_records=bodies.size()-selected;
    result.truncated=result.omitted_records!=0;
    if(limit<summary_reserve){result.omitted_records=bodies.size()+1;result.truncated=true;if(emission)*emission={};return result;}
    const auto parts=selected+1;
    std::string summary="matcher_snapshot metadata_total="+(snapshot.inventory_evaluated?std::to_string(snapshot.metadata_total):"unknown")+
        " codec="+(!snapshot.codec?"unknown":*snapshot.codec==Codec::Vorbis?"ogg":*snapshot.codec==Codec::Flac?"flac":*snapshot.codec==Codec::Mp3?"mp3":"aac")+
        " kind=summary mode="+(delta?std::string("delta base_snapshot=")+std::to_string(emission->snapshot_id):std::string("full"))+
        " metadata_kept="+std::to_string(snapshot.metadata_count)+
        " metadata_omitted="+(snapshot.inventory_evaluated?std::to_string(snapshot.metadata_total-snapshot.metadata_count):"unknown")+
        " competitors_total="+(snapshot.inventory_evaluated?std::to_string(snapshot.competitor_total):"unknown")+
        " competitors_kept="+std::to_string(snapshot.competitor_count)+
        " competitors_omitted="+(snapshot.inventory_evaluated?std::to_string(snapshot.competitor_total-snapshot.competitor_count):"unknown")+common+
        " inventory_evaluated="+std::to_string(snapshot.inventory_evaluated)+" abort_reason="+IntegerNumber(snapshot.abort_reason)+
        " path="+std::string(MatcherPathName(snapshot.path))+
        " source_duration_ms="+Number(snapshot.source_duration_ms)+" end_ms="+Number(snapshot.end_ms)+
        " ownership_state="+IntegerNumber(snapshot.ownership_state)+" ownership_reason="+IntegerNumber(snapshot.ownership_reason)+
        " queue_dropped="+IntegerNumber(snapshot.queue_dropped)+" truncated="+std::to_string(result.truncated)+
        " serialized_omitted="+std::to_string(result.omitted_records)+" part=1/"+std::to_string(parts);
    result.bytes=summary.size();result.records.push_back(std::move(summary));
    for(std::size_t i=0;i<selected;++i) {
        bodies[i]+=" part="+std::to_string(i+2)+"/"+std::to_string(parts);
        result.bytes+=bodies[i].size();result.records.push_back(std::move(bodies[i]));
    }
    // A truncated inventory cannot serve as a reconstructable delta base.
    if(emission)*emission=result.truncated?MatcherSnapshotEmissionState{}:next;
    return result;
}
}
inline SerializedMatcherSnapshot SerializeMatcherSnapshot(const MatcherSnapshot& snapshot,
    std::size_t limit=kMatcherSnapshotBytes) {return matcher_diagnostic_detail::Serialize(snapshot,limit);}
inline SerializedMatcherSnapshot SerializeMatcherSnapshot(const MatcherSnapshot& snapshot,
    MatcherSnapshotEmissionState& emission,std::size_t limit=kMatcherSnapshotBytes) {
    return matcher_diagnostic_detail::Serialize(snapshot,limit,&emission);
}
namespace matcher_diagnostic_detail {
class Fingerprint {
    uint64_t hash_=14695981039346656037ull;
public:
    template<class Value> void Add(Value value) noexcept {
        const auto* data=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t i=0;i<sizeof(Value);++i){hash_^=data[i];hash_*=1099511628211ull;}
    }
    template<class Value> void Add(std::optional<Value> value) noexcept {
        Add(bool(value));if(value)Add(*value);
    }
    void Add(std::optional<double> value) noexcept {
        if(value&&!std::isfinite(*value))value.reset();
        Add(bool(value));if(value)Add(*value);
    }
    uint64_t Value() const noexcept {return hash_;}
};
}
// Fingerprint all retained facts, including parts omitted by serialization.
// Snapshot sequence and terminal acknowledgement do not change candidate facts.
inline uint64_t MatcherSnapshotSignature(const MatcherSnapshot& snapshot) noexcept {
    matcher_diagnostic_detail::Fingerprint hash;
    hash.Add(snapshot.capture_id);hash.Add(snapshot.epoch);hash.Add(snapshot.path);
    hash.Add(snapshot.ownership_state);hash.Add(snapshot.ownership_reason);hash.Add(snapshot.queue_dropped);
    hash.Add(snapshot.association_metadata_id);hash.Add(snapshot.association_retained);
    // Absolute observation time does not change eligibility. Relative timing
    // evidence below remains part of the signature, including end deltas.
    hash.Add(snapshot.source_duration_ms);hash.Add(snapshot.complete);
    hash.Add(snapshot.inventory_evaluated);hash.Add(snapshot.abort_reason);
    if(snapshot.codec){hash.Add(true);hash.Add(unsigned(*snapshot.codec));}else hash.Add(false);
    // Raw byte progress does not change eligibility. It remains in emitted
    // snapshots and the forced terminal snapshot without logging every page.
    hash.Add(snapshot.subsequent_starts);hash.Add(snapshot.retirement_threshold);
    hash.Add(snapshot.time_offset_tolerance_ms);hash.Add(snapshot.duration_tolerance_ms);
    hash.Add(snapshot.known_pruned_metadata);hash.Add(snapshot.prior_history);
    hash.Add(snapshot.metadata_total);hash.Add(snapshot.competitor_total);
    hash.Add(snapshot.metadata_count);hash.Add(snapshot.competitor_count);
    for(std::size_t i=0;i<snapshot.metadata_count;++i) {
        const auto& item=snapshot.metadata[i];
        hash.Add(item.metadata_id);hash.Add(item.assigned);hash.Add(item.usable);
        hash.Add(item.timing_evaluated);hash.Add(item.duration_evaluated);hash.Add(item.decision);hash.Add(item.path);
        hash.Add(item.ownership_state);hash.Add(item.ownership_reason);
        hash.Add(item.first_seen_delta_ms);hash.Add(item.start_delta_ms);hash.Add(item.end_delta_ms);
        hash.Add(item.duration_delta_ms);hash.Add(item.source_duration_ms);hash.Add(item.metadata_duration_ms);
    }
    for(std::size_t i=0;i<snapshot.competitor_count;++i) {
        const auto& item=snapshot.competitors[i];
        hash.Add(item.capture_id);hash.Add(item.complete);hash.Add(item.associated);hash.Add(item.duration_ms);
    }
    return hash.Value();
}
struct MatcherBudgetAdmission {
    bool admitted=false,emit_exhaustion=false;
    uint64_t omitted_snapshots=0,omitted_records=0;
};
class MatcherDiagnosticBudget {
    std::size_t limit_,used_=0;
    uint64_t omitted_snapshots_=0,omitted_records_=0;
    bool exhaustion_reported_=false;
public:
    explicit MatcherDiagnosticBudget(std::size_t limit=kMatcherSessionBytes):limit_(std::min(limit,kMatcherSessionBytes)){}
    MatcherBudgetAdmission Admit(std::size_t bytes,std::size_t records) noexcept {
        if(bytes<=limit_-used_){used_+=bytes;return {true,false,omitted_snapshots_,omitted_records_};}
        ++omitted_snapshots_;omitted_records_+=records;
        const bool emit=!exhaustion_reported_;exhaustion_reported_=true;
        return {false,emit,omitted_snapshots_,omitted_records_};
    }
    std::size_t Used() const noexcept {return used_;}
};
}
