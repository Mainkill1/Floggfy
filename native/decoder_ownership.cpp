#include "decoder_ownership.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ownership {
namespace {

bool ValidTime(double value) noexcept {
    return std::isfinite(value)&&value>=0;
}

template<class Record,std::size_t N>
Record* EmptyOrOldest(std::array<Record,N>& records) noexcept {
    for(auto& record:records)if(!record.token)return &record;
    return &*std::min_element(records.begin(),records.end(),[](const auto& left,const auto& right) {
        return left.sequence<right.sequence;
    });
}

} // namespace

const char* OwnershipReasonName(OwnershipReason reason) noexcept {
    switch(reason) {
    case OwnershipReason::CaptureBegan:return "capture-began";
    case OwnershipReason::DecodedOutputObserved:return "decoded-output-observed";
    case OwnershipReason::PlaybackStageBound:return "playback-stage-bound";
    case OwnershipReason::CaptureAborted:return "capture-aborted";
    case OwnershipReason::InvalidCaptureBegin:return "invalid-capture-begin";
    case OwnershipReason::InvalidCompletion:return "invalid-completion";
    case OwnershipReason::OwnerReplaced:return "owner-capacity-replaced";
    case OwnershipReason::PlaybackReplaced:return "playback-capacity-replaced";
    case OwnershipReason::OwnerUnavailable:return "owner-unavailable";
    case OwnershipReason::InvalidDecodedRange:return "invalid-decoded-range";
    case OwnershipReason::DecodedOrderRegression:return "decoded-order-regression";
    case OwnershipReason::DecodedBeforeCapture:return "decoded-before-capture";
    case OwnershipReason::AmbiguousDecoderLifetime:return "ambiguous-decoder-lifetime";
    case OwnershipReason::AmbiguousSuccessor:return "ambiguous-successor";
    case OwnershipReason::PlaybackOrderRegression:return "playback-order-regression";
    case OwnershipReason::CausalOrderAmbiguous:return "causal-order-ambiguous";
    case OwnershipReason::ObservationLost:return "observation-lost";
    case OwnershipReason::MissingCapture:return "missing-capture";
    }
    return "unknown";
}

bool NormalizeDecoderOwner(std::uintptr_t capture_context,
                           std::int32_t capture_to_decoder,
                           std::uintptr_t& owner) noexcept {
    owner=0;if(!capture_context)return false;
    std::uintptr_t result=0;
    if(capture_to_decoder>=0) {
        const auto delta=std::uintptr_t(std::uint32_t(capture_to_decoder));
        if(capture_context>std::numeric_limits<std::uintptr_t>::max()-delta)return false;
        result=capture_context+delta;
    } else {
        const auto delta=std::uint64_t(-std::int64_t(capture_to_decoder));
        if(capture_context<delta)return false;
        result=capture_context-std::uintptr_t(delta);
    }
    if(!result||result%alignof(void*))return false;
    owner=result;return true;
}

DecoderOwnershipTracker::CaptureRecord*
DecoderOwnershipTracker::FindCapture(std::uint64_t token) noexcept {
    for(auto& capture:captures_)if(capture.token==token)return &capture;
    return nullptr;
}

const DecoderOwnershipTracker::CaptureRecord*
DecoderOwnershipTracker::FindCapture(std::uint64_t token) const noexcept {
    for(const auto& capture:captures_)if(capture.token==token)return &capture;
    return nullptr;
}

DecoderOwnershipTracker::OwnerRecord*
DecoderOwnershipTracker::FindOwner(std::uintptr_t owner) noexcept {
    for(auto& record:owners_)if(record.token&&record.owner==owner)return &record;
    return nullptr;
}

DecoderOwnershipTracker::PlaybackRecord*
DecoderOwnershipTracker::FindPlayback(const std::string& id) noexcept {
    for(auto& record:playbacks_)if(record.token&&record.id==id)return &record;
    return nullptr;
}

DecoderOwnershipTracker::PlaybackRecord*
DecoderOwnershipTracker::FindPlayback(std::uint64_t token) noexcept {
    for(auto& record:playbacks_)if(record.token==token)return &record;
    return nullptr;
}

void DecoderOwnershipTracker::InvalidateOwner(std::uintptr_t owner,OwnershipReason reason) noexcept {
    for(auto& capture:captures_) {
        if(capture.token&&capture.owner==owner) {
            capture.state=OwnershipState::Invalidated;capture.reason=reason;
        }
    }
    const auto current=FindCapture(current_capture_);
    if(current&&current->owner==owner)current_capture_=current_playback_=0;
    const auto pending=FindCapture(pending_capture_);
    if(pending&&pending->owner==owner)pending_capture_=0;
}

void DecoderOwnershipTracker::InvalidatePlayback(std::uint64_t playback,OwnershipReason reason) noexcept {
    if(!playback)return;
    for(auto& capture:captures_) {
        if(capture.token&&capture.playback_token==playback)
            capture.state=OwnershipState::Invalidated,capture.reason=reason;
    }
    if(current_playback_==playback)current_playback_=0;
    if(latest_playback_==playback)latest_playback_=0;
}

DecoderOwnershipTracker::OwnerRecord&
DecoderOwnershipTracker::EnsureOwner(std::uintptr_t owner,unsigned epoch) {
    if(auto* existing=FindOwner(owner))return *existing;
    auto* slot=EmptyOrOldest(owners_);
    if(slot->token)InvalidateOwner(slot->owner,OwnershipReason::OwnerReplaced);
    *slot={};slot->owner=owner;slot->token=next_owner_++;slot->sequence=sequence_++;
    slot->epoch=epoch;return *slot;
}

DecoderOwnershipTracker::PlaybackRecord&
DecoderOwnershipTracker::EnsurePlayback(const std::string& id,const std::string& uri,
                                        std::uint64_t generation,double observed) {
    if(auto* existing=FindPlayback(id)) {
        if(!uri.empty())existing->uri=uri;
        if(generation)existing->listen_generation=generation;
        if(observed>existing->observed)existing->observed=observed;
        return *existing;
    }
    auto* slot=EmptyOrOldest(playbacks_);
    if(slot->token)InvalidatePlayback(slot->token,OwnershipReason::PlaybackReplaced);
    *slot={};slot->id=id;slot->uri=uri;slot->token=next_playback_++;
    slot->sequence=sequence_++;slot->listen_generation=generation;
    slot->first_observed=observed;slot->observed=observed;
    return *slot;
}

std::uint64_t DecoderOwnershipTracker::BeginCapture(DecoderCodec codec,std::uintptr_t owner,
    std::uint32_t stream,double time,unsigned epoch,std::uint64_t producer_sequence) {
    auto* slot=EmptyOrOldest(captures_);
    if(slot->token) {
        if(current_capture_==slot->token)current_capture_=current_playback_=0;
        if(pending_capture_==slot->token)pending_capture_=0;
    }
    *slot={};slot->token=next_capture_++;slot->sequence=sequence_++;slot->codec=codec;
    slot->owner=owner;slot->stream=stream;slot->begun=time;slot->epoch=epoch;slot->active=true;
    slot->begun_sequence=producer_sequence;
    if(!owner||epoch!=epoch_||!ValidTime(time)) {
        slot->state=OwnershipState::Invalidated;slot->reason=OwnershipReason::InvalidCaptureBegin;
    } else {
        slot->owner_token=EnsureOwner(owner,epoch).token;
    }
    return slot->token;
}

void DecoderOwnershipTracker::CompleteCapture(std::uint64_t capture_token,double time,
                                               std::uint64_t producer_sequence) {
    auto* capture=FindCapture(capture_token);if(!capture)return;
    if(!ValidTime(time)||time<capture->begun)Conflict(*capture,OwnershipReason::InvalidCompletion);
    capture->active=false;capture->complete=true;capture->completed_at=time;
    capture->completed_sequence=producer_sequence;
}

void DecoderOwnershipTracker::AbortCapture(std::uint64_t capture_token,CaptureAbortReason reason) {
    auto* capture=FindCapture(capture_token);if(!capture)return;
    capture->clean_restart_abort=reason==CaptureAbortReason::Reinitialized&&
        capture->active&&!capture->complete&&
        (capture->state==OwnershipState::AwaitingOutput||
         capture->state==OwnershipState::AwaitingPlaybackInstance||
         capture->state==OwnershipState::Bound);
    capture->active=false;capture->complete=false;capture->state=OwnershipState::Invalidated;
    capture->reason=OwnershipReason::CaptureAborted;
    if(current_capture_==capture_token)current_capture_=current_playback_=0;
    if(pending_capture_==capture_token)pending_capture_=0;
}

void DecoderOwnershipTracker::Conflict(CaptureRecord& capture,OwnershipReason reason) noexcept {
    if(capture.state!=OwnershipState::Invalidated) {
        capture.state=OwnershipState::Conflict;capture.reason=reason;
        if(reason!=OwnershipReason::AmbiguousSuccessor)capture.ambiguity_group=0;
    }
    if(current_capture_==capture.token)current_capture_=current_playback_=0;
    if(pending_capture_==capture.token)pending_capture_=0;
}

void DecoderOwnershipTracker::ConflictSuccessors(CaptureRecord* first,CaptureRecord* second,
                                                  CaptureRecord* third) noexcept {
    const auto group=next_ambiguity_++;
    CaptureRecord* records[]={first,second,third};
    for(auto* capture:records) {
        if(!capture||capture->state==OwnershipState::Invalidated)continue;
        capture->ambiguity_group=group;
        Conflict(*capture,OwnershipReason::AmbiguousSuccessor);
    }
}

void DecoderOwnershipTracker::ObserveDecoded(const DecodedActivity& activity) {
    if(!activity.owner||!activity.calls||!activity.produced_floats)return;
    auto* owner=FindOwner(activity.owner);
    if(!owner||activity.epoch!=epoch_||owner->epoch!=activity.epoch) {
        InvalidateOwner(activity.owner,OwnershipReason::OwnerUnavailable);return;
    }
    if(!ValidTime(activity.first)||!ValidTime(activity.last)||activity.last<activity.first||
       (activity.last_sequence&&activity.last_sequence<activity.sequence)) {
        for(auto& capture:captures_)if(capture.token&&capture.owner==activity.owner&&capture.active)
            Conflict(capture,OwnershipReason::InvalidDecodedRange);
        return;
    }
    std::array<CaptureRecord*,kCaptureCapacity> candidates{};
    unsigned candidate_count=0;
    if(activity.sequence) {
        std::uint64_t latest_begin=0;
        for(const auto& capture:captures_) {
            if(capture.token&&capture.owner==activity.owner&&capture.begun_sequence<=activity.sequence)
                latest_begin=std::max(latest_begin,capture.begun_sequence);
        }
        if(!latest_begin)return;
        const auto last_sequence=activity.last_sequence?activity.last_sequence:activity.sequence;
        // A new stream begins a new decoder lifetime even while the old
        // compressed reader remains active. Include terminal lifetimes as
        // barriers: their output must never fall back to an older reader.
        // A batch spanning another begin (or tied begins) is still ambiguous.
        for(auto& capture:captures_) {
            if(capture.token&&capture.owner==activity.owner&&
               capture.begun_sequence>=latest_begin&&capture.begun_sequence<=last_sequence)
                candidates[candidate_count++]=&capture;
        }
    } else {
        for(auto& capture:captures_) {
            if(!capture.token||capture.owner!=activity.owner||
               capture.state==OwnershipState::Invalidated||capture.state==OwnershipState::Conflict)
                continue;
            const bool contains=capture.active ? activity.first>=capture.begun :
                capture.complete&&activity.first>=capture.begun&&activity.last<=capture.completed_at;
            if(contains)candidates[candidate_count++]=&capture;
        }
    }
    if(!candidate_count)return;
    auto* candidate=candidates[0];
    if(candidate_count==1&&(candidate->state==OwnershipState::Invalidated||
       candidate->state==OwnershipState::Conflict))return;
    // Distinct decoder owners routinely overlap during preload. Ordering is
    // meaningful only within one normalized owner; playback identity and the
    // capture lifecycle disambiguate owners from each other.
    if(owner->last&&activity.first<owner->last) {
        for(auto& capture:captures_)if(capture.token&&capture.owner==activity.owner&&capture.active)
            Conflict(capture,OwnershipReason::DecodedOrderRegression);
        return;
    }
    owner->first=owner->first?owner->first:activity.first;owner->last=activity.last;
    if(candidate_count!=1) {
        DecoderConflictEvidence evidence;evidence.activity=activity;
        // Preserve the actual competing lifetimes before Conflict changes eligibility.
        for(unsigned index=0;index<candidate_count;++index)
            evidence.captures[evidence.count++]=candidates[index]->token;
        for(unsigned index=0;index<candidate_count;++index) {
            auto& capture=*candidates[index];
            if(capture.state==OwnershipState::Invalidated||capture.state==OwnershipState::Conflict)continue;
            capture.conflict_evidence=evidence;
            Conflict(capture,OwnershipReason::AmbiguousDecoderLifetime);
        }
        return;
    }
    if(candidate->state==OwnershipState::Conflict)return;
    const auto was_bound=candidate->state==OwnershipState::Bound;
    // Capture-begin and the first decoded callback use a millisecond boot clock.
    // Equality is routine for this already-proven owner edge; only a genuinely
    // earlier decoded observation contradicts the lifecycle.
    if(activity.first<candidate->begun) {
        Conflict(*candidate,OwnershipReason::DecodedBeforeCapture);return;
    }
    candidate->first_output=candidate->first_output?candidate->first_output:activity.first;
    candidate->last_output=activity.last;
    if(was_bound)return;
    candidate->state=OwnershipState::AwaitingPlaybackInstance;
    candidate->reason=OwnershipReason::DecodedOutputObserved;

    auto* current=FindCapture(current_capture_);
    if(!current_capture_||!current||current->state==OwnershipState::Invalidated) {
        current_capture_=candidate->token;
    } else if(current->token!=candidate->token) {
        if(current->complete&&current->state!=OwnershipState::Bound&&
           candidate->begun_sequence&&current->completed_sequence&&
           candidate->begun_sequence>current->completed_sequence) {
            current_capture_=candidate->token;current_playback_=0;return;
        }
        if(current->state!=OwnershipState::Bound||!current_playback_) {
            // A decoder successor normally starts before the outgoing decoder
            // emits its final compressed frame. Keep exactly one successor
            // stage; the next playback transition decides which lifecycle it
            // belongs to. More than one unresolved successor is contradictory.
            const bool mixed_voice_and_music=
                (current->codec==DecoderCodec::Mp3)!=(candidate->codec==DecoderCodec::Mp3);
            const bool ordered_successor=mixed_voice_and_music||
                (candidate->begun_sequence&&current->begun_sequence?
                 candidate->begun_sequence>current->begun_sequence:candidate->begun>current->begun);
            if(ordered_successor&&(!pending_capture_||pending_capture_==candidate->token))
                pending_capture_=candidate->token;
            else {
                auto* pending=FindCapture(pending_capture_);
                ConflictSuccessors(ordered_successor?nullptr:current,pending,candidate);
            }
            return;
        }
        if(pending_capture_&&pending_capture_!=candidate->token) {
            ConflictSuccessors(FindCapture(pending_capture_),candidate);return;
        }
        pending_capture_=candidate->token;
    }

    if(candidate->token==current_capture_&&!pending_capture_&&latest_playback_) {
        if(auto* playback=FindPlayback(latest_playback_);
           playback&&playback->observed>candidate->first_output)
            TryBind(*candidate,*playback,playback->observed);
    }
}

bool DecoderOwnershipTracker::ReconsiderAmbiguousSuccessor(
    std::uint64_t capture_token) noexcept {
    auto* capture=FindCapture(capture_token);
    if(!capture||capture->state!=OwnershipState::Conflict||
       capture->reason!=OwnershipReason::AmbiguousSuccessor||
       !capture->ambiguity_group||!capture->first_output||
       (!capture->active&&!capture->complete))return false;
    for(const auto& other:captures_) {
        if(other.token==capture->token||other.ambiguity_group!=capture->ambiguity_group)continue;
        if((other.active||other.complete)&&other.state==OwnershipState::Conflict&&
           other.reason==OwnershipReason::AmbiguousSuccessor)return false;
    }
    if(pending_capture_&&pending_capture_!=capture->token)return false;
    capture->state=OwnershipState::AwaitingPlaybackInstance;
    capture->reason=OwnershipReason::DecodedOutputObserved;
    capture->ambiguity_group=0;
    pending_capture_=capture->token;
    return true;
}

void DecoderOwnershipTracker::TryBind(CaptureRecord& capture,PlaybackRecord& playback,
                                      double observed) noexcept {
    if(capture.state!=OwnershipState::AwaitingPlaybackInstance||!capture.first_output)return;
    if(playback.bound_capture&&playback.bound_capture!=capture.token)return;
    if(!ValidTime(observed))return;
    // Ready playback and decoder callbacks come from independent producers.
    // Their timestamps are useful diagnostics, not a shared causal clock. The
    // worker's consumed Ready stage is the ordering evidence.
    if(pending_capture_==capture.token&&playback.token==current_playback_)return;
    capture.playback_token=playback.token;capture.listen_generation=playback.listen_generation;
    playback.bound_capture=capture.token;
    capture.state=OwnershipState::Bound;current_capture_=capture.token;
    capture.reason=OwnershipReason::PlaybackStageBound;
    current_playback_=playback.token;latest_playback_=playback.token;
    if(pending_capture_==capture.token)pending_capture_=0;
}

bool DecoderOwnershipTracker::BindCapture(std::uint64_t capture_token,
    const std::string& playback_id,const std::string& uri,
    std::uint64_t listen_generation,double observed) {
    if(!listen_generation||!ValidTime(observed))return false;
    auto* capture=FindCapture(capture_token);if(!capture)return false;

    PlaybackRecord* playback=nullptr;
    if(playback_id.empty()) {
        auto* latest=FindPlayback(latest_playback_);
        if(latest&&!latest->listen_generation&&
           (uri.empty()||latest->uri.empty()||latest->uri==uri))playback=latest;
        else playback=&EnsurePlayback("\x1flisten:"+std::to_string(listen_generation),
                                      uri,listen_generation,observed);
    } else playback=&EnsurePlayback(playback_id,uri,listen_generation,observed);
    if(!playback)return false;
    if(!uri.empty())playback->uri=uri;
    playback->listen_generation=listen_generation;
    if(observed>playback->observed)playback->observed=observed;
    latest_playback_=playback->token;

    if(capture->state==OwnershipState::Bound) {
        if(capture->playback_token!=playback->token)return false;
        capture->listen_generation=listen_generation;return true;
    }
    if(capture->state!=OwnershipState::AwaitingPlaybackInstance||!capture->first_output||
       (playback->bound_capture&&playback->bound_capture!=capture->token))return false;
    if(pending_capture_==capture->token&&playback->token==current_playback_)return false;
    capture->playback_token=playback->token;
    capture->listen_generation=listen_generation;
    playback->bound_capture=capture->token;
    capture->state=OwnershipState::Bound;
    capture->reason=OwnershipReason::PlaybackStageBound;
    current_capture_=capture->token;current_playback_=playback->token;
    if(pending_capture_==capture->token)pending_capture_=0;
    return true;
}

bool DecoderOwnershipTracker::BindCaptureExact(std::uint64_t capture_token,
    const std::string& playback_id,const std::string& uri,
    std::uint64_t listen_generation,double observed,bool source_complete,const char** rejection_reason) {
    if(rejection_reason)*rejection_reason=nullptr;
    const auto reject=[&](const char* reason) {
        if(rejection_reason)*rejection_reason=reason;
        return false;
    };
    if(playback_id.empty())return reject("missing-playback-id");
    if(!listen_generation)return reject("missing-listen-generation");
    if(!ValidTime(observed))return reject("invalid-observation-time");
    auto* capture=FindCapture(capture_token);
    if(!capture)return reject("missing-capture");
    if(!capture->active&&!capture->complete)return reject("inactive-incomplete-capture");
    // A validated complete source and its unique exact native identity can be
    // available before PCM output. The caller supplies source validation; the
    // tracker independently requires a completed, non-conflicting lifecycle.
    const bool complete_without_output=source_complete&&capture->complete&&
        (capture->state==OwnershipState::AwaitingOutput||capture->state==OwnershipState::Bound);
    if(!capture->first_output&&!complete_without_output) {
        if(capture->state==OwnershipState::Conflict)return reject("ownership-conflict");
        if(capture->state==OwnershipState::Invalidated)return reject("ownership-invalidated");
        if(source_complete&&!capture->complete)return reject("source-lifecycle-not-complete");
        return reject("no-output-or-complete-source");
    }
    // A sample-zero restart may replace an incomplete capture after exact
    // native identity and complete compressed coverage have been revalidated.
    // Retain the old claim until that proof arrives; weak matching cannot reuse it.
    auto* prior_playback=FindPlayback(playback_id);
    bool replace_restart=false;
    if(prior_playback&&prior_playback->bound_capture&&
       prior_playback->bound_capture!=capture->token) {
        const auto* prior=FindCapture(prior_playback->bound_capture);
        if(!source_complete||!capture->complete)return reject("restart-source-not-complete");
        if(capture->codec!=DecoderCodec::Flac)return reject("restart-codec-not-flac");
        if(!prior)return reject("restart-predecessor-unavailable");
        if(!prior->clean_restart_abort||prior->active||prior->complete||
           prior->state!=OwnershipState::Invalidated||prior->reason!=OwnershipReason::CaptureAborted)
            return reject("restart-predecessor-not-eligible");
        if(prior->codec!=capture->codec||prior->owner!=capture->owner||
           prior->owner_token!=capture->owner_token||prior->epoch!=capture->epoch)
            return reject("restart-owner-or-epoch-mismatch");
        if(prior->sequence>=capture->sequence||prior->begun>capture->begun)
            return reject("restart-order-invalid");
        if(prior->listen_generation!=listen_generation||prior_playback->listen_generation!=listen_generation||
           uri.empty()||prior_playback->uri!=uri)return reject("restart-playback-identity-mismatch");
        for(const auto& between:captures_) {
            if(between.owner!=capture->owner||between.sequence<=prior->sequence||
               between.sequence>=capture->sequence)continue;
            if(!between.clean_restart_abort||between.active||between.complete||
               between.state!=OwnershipState::Invalidated||
               between.reason!=OwnershipReason::CaptureAborted||
               between.owner_token!=capture->owner_token||between.epoch!=capture->epoch||
               between.codec!=capture->codec)return reject("restart-chain-not-eligible");
        }
        replace_restart=true;
    }
    auto& playback=EnsurePlayback(playback_id,uri,listen_generation,observed);
    if(!uri.empty())playback.uri=uri;
    playback.listen_generation=listen_generation;
    if(observed>playback.observed)playback.observed=observed;
    latest_playback_=playback.token;

    if(capture->state==OwnershipState::Bound) {
        if(capture->playback_token!=playback.token)return reject("capture-bound-to-other-playback");
        capture->listen_generation=listen_generation;return true;
    }
    const bool exact_conflict=capture->state==OwnershipState::Conflict&&
        capture->reason==OwnershipReason::AmbiguousSuccessor;
    if(capture->state!=OwnershipState::AwaitingPlaybackInstance&&!exact_conflict&&!complete_without_output)
        return reject(capture->state==OwnershipState::Conflict?"ownership-conflict":"ownership-state-excluded");
    if(playback.bound_capture&&playback.bound_capture!=capture->token&&!replace_restart)
        return reject("playback-already-claimed");
    capture->playback_token=playback.token;
    capture->listen_generation=listen_generation;
    capture->ambiguity_group=0;
    playback.bound_capture=capture->token;
    capture->state=OwnershipState::Bound;
    capture->reason=OwnershipReason::PlaybackStageBound;
    current_capture_=capture->token;current_playback_=playback.token;
    pending_capture_=0;
    return true;
}

void DecoderOwnershipTracker::ObservePlaybackImpl(const std::string& playback_id,
    const std::string& uri,std::uint64_t listen_generation,double observed,
    bool successor_transition) {
    if(playback_id.empty()||!ValidTime(observed))return;
    auto& playback=EnsurePlayback(playback_id,uri,listen_generation,observed);
    playback.successor_transition=playback.successor_transition||successor_transition;
    latest_playback_=playback.token;
    auto* current=current_capture_?FindCapture(current_capture_):nullptr;
    auto* pending=pending_capture_?FindCapture(pending_capture_):nullptr;
    CaptureRecord* target=pending?pending:current;
    if(current&&pending) {
        // A repeated identity created before the successor decoder belongs to
        // the outgoing stage. A positively identified successor transition,
        // or an identity first created after that decoder, belongs to pending.
        const bool outgoing_identity=playback.token==current_playback_||
            (!playback.successor_transition&&playback.sequence<pending->sequence);
        target=outgoing_identity?current:pending;
    }
    const bool episode=uri.rfind("spotify:episode:",0)==0;
    const bool track=uri.rfind("spotify:track:",0)==0;
    if(track||episode) {
        if(current&&pending&&
           (current->codec==DecoderCodec::Mp3)!=(pending->codec==DecoderCodec::Mp3))
            target=(current->codec==DecoderCodec::Mp3)==episode?current:pending;
        else if(target&&(target->codec==DecoderCodec::Mp3)!=episode)target=nullptr;
    }
    if(!target)return;
    if(target->state==OwnershipState::Bound&&target->playback_token==playback.token&&
       listen_generation) {
        target->listen_generation=listen_generation;return;
    }
    TryBind(*target,playback,observed);
}

void DecoderOwnershipTracker::ObservePlayback(const std::string& playback_id,
    const std::string& uri,std::uint64_t listen_generation,double observed) {
    if(playback_id.empty()) {
        // A Ready media snapshot with a newly assigned listen generation is a
        // concrete lifecycle stage even when Spotify withholds playbackId.
        // Keep the synthetic identity private to this tracker; callers still
        // treat the public playback ID as unavailable.
        if(!listen_generation)return;
        // The client can identify a buffering successor while the following
        // Ready observation comes from Windows and therefore has no Spotify
        // playback ID. Resolve that already-observed lifecycle instead of
        // inventing an unrelated playback record.
        if(auto* latest=FindPlayback(latest_playback_);
           latest&&latest->successor_transition&&!latest->listen_generation&&
           (uri.empty()||latest->uri.empty()||latest->uri==uri)) {
            ObservePlaybackImpl(latest->id,uri,listen_generation,observed,false);
            return;
        }
        ObservePlaybackImpl("\x1flisten:"+std::to_string(listen_generation),uri,
                            listen_generation,observed,false);
        return;
    }
    ObservePlaybackImpl(playback_id,uri,listen_generation,observed,false);
}

void DecoderOwnershipTracker::ObserveTransition(const std::string& playback_id,
    const std::string& uri,double transition_time,bool explicit_skip,bool natural_transition) {
    if(playback_id.empty()||!ValidTime(transition_time))return;
    // A transition closes or quarantines the outgoing listen. It can name the
    // successor lifecycle for a later Ready stage, but it cannot assign media
    // metadata to a decoder by itself.
    auto& playback=EnsurePlayback(playback_id,uri,0,transition_time);
    playback.successor_transition=playback.successor_transition||explicit_skip||natural_transition;
    latest_playback_=playback.token;
}

void DecoderOwnershipTracker::ObservationLost(ObservationLoss,double time) {
    (void)time;
    for(auto& capture:captures_) {
        capture.clean_restart_abort=false;
        if(capture.token&&(capture.active||capture.complete)) {
            capture.state=OwnershipState::Invalidated;
            capture.reason=OwnershipReason::ObservationLost;
        }
    }
    current_capture_=pending_capture_=current_playback_=latest_playback_=0;
}

void DecoderOwnershipTracker::Reset(unsigned epoch) {
    captures_={};owners_={};playbacks_={};
    current_capture_=pending_capture_=current_playback_=latest_playback_=0;
    next_ambiguity_=1;
    epoch_=epoch;
}

OwnershipBinding DecoderOwnershipTracker::Binding(std::uint64_t capture_token) const {
    OwnershipBinding binding{};binding.capture_token=capture_token;
    const auto* capture=FindCapture(capture_token);
    if(!capture) {
        binding.state=OwnershipState::Invalidated;binding.reason=OwnershipReason::MissingCapture;return binding;
    }
    binding.playback_token=capture->playback_token;
    binding.owner_token=capture->owner_token;binding.listen_generation=capture->listen_generation;
    binding.state=capture->state;
    binding.reason=capture->reason;
    return binding;
}

const DecoderConflictEvidence* DecoderOwnershipTracker::ConflictEvidence(std::uint64_t token) const noexcept {
    const auto* capture=FindCapture(token);
    return capture&&capture->conflict_evidence.count?&capture->conflict_evidence:nullptr;
}

std::uint64_t DecoderOwnershipTracker::OwnerToken(std::uintptr_t owner) const noexcept {
    for(const auto& record:owners_)if(record.token&&record.owner==owner)return record.token;
    return 0;
}

std::uint64_t DecoderOwnershipTracker::PlaybackToken(const std::string& playback_id) const noexcept {
    for(const auto& record:playbacks_)if(record.token&&record.id==playback_id)return record.token;
    return 0;
}

} // namespace ownership
