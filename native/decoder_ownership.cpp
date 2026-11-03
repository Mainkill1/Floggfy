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
        existing->uri=uri;existing->listen_generation=generation;
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
    if(!ValidTime(time)||time<=capture->begun)Conflict(*capture,OwnershipReason::InvalidCompletion);
    capture->active=false;capture->complete=true;capture->completed_at=time;
    capture->completed_sequence=producer_sequence;
}

void DecoderOwnershipTracker::AbortCapture(std::uint64_t capture_token,CaptureAbortReason) {
    auto* capture=FindCapture(capture_token);if(!capture)return;
    capture->active=false;capture->complete=false;capture->state=OwnershipState::Invalidated;
    capture->reason=OwnershipReason::CaptureAborted;
    if(current_capture_==capture_token)current_capture_=current_playback_=0;
    if(pending_capture_==capture_token)pending_capture_=0;
}

void DecoderOwnershipTracker::Conflict(CaptureRecord& capture,OwnershipReason reason) noexcept {
    if(capture.state!=OwnershipState::Invalidated) {
        capture.state=OwnershipState::Conflict;capture.reason=reason;
    }
    if(current_capture_==capture.token)current_capture_=current_playback_=0;
    if(pending_capture_==capture.token)pending_capture_=0;
}

void DecoderOwnershipTracker::ObserveDecoded(const DecodedActivity& activity) {
    if(!activity.owner||!activity.calls||!activity.produced_floats)return;
    auto* owner=FindOwner(activity.owner);
    if(!owner||activity.epoch!=epoch_||owner->epoch!=activity.epoch) {
        InvalidateOwner(activity.owner,OwnershipReason::OwnerUnavailable);return;
    }
    if(!ValidTime(activity.first)||!ValidTime(activity.last)||activity.last<activity.first) {
        for(auto& capture:captures_)if(capture.token&&capture.owner==activity.owner&&capture.active)
            Conflict(capture,OwnershipReason::InvalidDecodedRange);
        return;
    }
    CaptureRecord* candidate=nullptr;unsigned candidates=0;
    CaptureRecord* latest_lifetime=nullptr;
    if(activity.sequence) {
        for(auto& capture:captures_) {
            if(!capture.token||capture.owner!=activity.owner||!capture.begun_sequence||
               capture.begun_sequence>activity.sequence)continue;
            if(!latest_lifetime||capture.begun_sequence>latest_lifetime->begun_sequence)
                latest_lifetime=&capture;
            if(capture.active&&capture.state!=OwnershipState::Invalidated&&
               capture.state!=OwnershipState::Conflict) {candidate=&capture;++candidates;}
        }
        if(!candidates&&latest_lifetime&&latest_lifetime->state!=OwnershipState::Invalidated&&
           latest_lifetime->state!=OwnershipState::Conflict) {candidate=latest_lifetime;candidates=1;}
    } else {
        for(auto& capture:captures_) {
            if(!capture.token||capture.owner!=activity.owner||
               capture.state==OwnershipState::Invalidated||capture.state==OwnershipState::Conflict)
                continue;
            const bool contains=capture.active ? activity.first>=capture.begun :
                capture.complete&&activity.first>=capture.begun&&activity.last<=capture.completed_at;
            if(contains){candidate=&capture;++candidates;}
        }
    }
    if(!candidates)return;
    // Distinct decoder owners routinely overlap during preload. Ordering is
    // meaningful only within one normalized owner; playback identity and the
    // capture lifecycle disambiguate owners from each other.
    if(owner->last&&activity.first<owner->last) {
        for(auto& capture:captures_)if(capture.token&&capture.owner==activity.owner&&capture.active)
            Conflict(capture,OwnershipReason::DecodedOrderRegression);
        return;
    }
    owner->first=owner->first?owner->first:activity.first;owner->last=activity.last;
    if(candidates!=1) {
        for(auto& capture:captures_) {
            if(!capture.token||capture.owner!=activity.owner||
               capture.state==OwnershipState::Invalidated||capture.state==OwnershipState::Conflict)
                continue;
            const bool contains=activity.sequence&&capture.begun_sequence?
                capture.active&&capture.begun_sequence<=activity.sequence:
                (capture.active ? activity.first>=capture.begun :
                 capture.complete&&activity.first>=capture.begun&&activity.last<=capture.completed_at);
            if(contains)Conflict(capture,OwnershipReason::AmbiguousDecoderLifetime);
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
                if(!ordered_successor)Conflict(*current,OwnershipReason::AmbiguousSuccessor);
                if(auto* pending=FindCapture(pending_capture_))
                    Conflict(*pending,OwnershipReason::AmbiguousSuccessor);
                Conflict(*candidate,OwnershipReason::AmbiguousSuccessor);
            }
            return;
        }
        if(pending_capture_&&pending_capture_!=candidate->token) {
            if(auto* pending=FindCapture(pending_capture_))
                Conflict(*pending,OwnershipReason::AmbiguousSuccessor);
            Conflict(*candidate,OwnershipReason::AmbiguousSuccessor);return;
        }
        pending_capture_=candidate->token;
    }

    if(candidate->token==current_capture_&&!pending_capture_&&latest_playback_) {
        if(auto* playback=FindPlayback(latest_playback_);
           playback&&playback->observed>candidate->first_output)
            TryBind(*candidate,*playback,playback->observed);
    }
}

void DecoderOwnershipTracker::TryBind(CaptureRecord& capture,PlaybackRecord& playback,
                                      double observed) noexcept {
    if(capture.state!=OwnershipState::AwaitingPlaybackInstance||!capture.first_output)return;
    if(playback.bound_capture&&playback.bound_capture!=capture.token)return;
    if(!ValidTime(observed)) {
        Conflict(capture,OwnershipReason::PlaybackOrderRegression);return;
    }
    // A transition snapshot can predate the first decoded callback for its new
    // owner and be drained afterward. It is not binding evidence yet; the next
    // fresh Ready observation must establish the ordering. Exact equality is
    // still ambiguous across queues and remains a conflict.
    if(observed<capture.first_output)return;
    if(observed==capture.first_output) {
        Conflict(capture,OwnershipReason::CausalOrderAmbiguous);return;
    }
    if(pending_capture_==capture.token&&playback.token==current_playback_)return;
    capture.playback_token=playback.token;capture.listen_generation=playback.listen_generation;
    playback.bound_capture=capture.token;
    capture.state=OwnershipState::Bound;current_capture_=capture.token;
    capture.reason=OwnershipReason::PlaybackStageBound;
    current_playback_=playback.token;latest_playback_=playback.token;
    if(pending_capture_==capture.token)pending_capture_=0;
}

void DecoderOwnershipTracker::ObservePlaybackImpl(const std::string& playback_id,
    const std::string& uri,std::uint64_t listen_generation,double observed,
    bool successor_transition) {
    if(playback_id.empty()||!ValidTime(observed))return;
    auto* prior=FindPlayback(playback_id);
    auto* latest=FindPlayback(latest_playback_);
    if(latest&&latest!=prior&&observed<=latest->observed) {
        if(auto* target=FindCapture(pending_capture_?pending_capture_:current_capture_))
            Conflict(*target,OwnershipReason::PlaybackOrderRegression);
        return;
    }
    if(prior&&observed<=prior->observed) {
        if(observed==prior->observed&&listen_generation) {
            for(auto& capture:captures_)
                if(capture.token&&capture.state==OwnershipState::Bound&&
                   capture.playback_token==prior->token)
                    capture.listen_generation=listen_generation;
        }
        // The media collector can repeat one buffering transition with small
        // timestamp jitter. Same-ID stale observations provide no new evidence;
        // ignore them and require a later fresh observation to bind.
        return;
    }
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
        ObservePlaybackImpl("\x1flisten:"+std::to_string(listen_generation),uri,
                            listen_generation,observed,false);
        return;
    }
    ObservePlaybackImpl(playback_id,uri,listen_generation,observed,false);
}

void DecoderOwnershipTracker::ObserveTransition(const std::string& playback_id,
    const std::string& uri,double transition_time,bool explicit_skip,bool natural_transition) {
    ObservePlaybackImpl(playback_id,uri,0,transition_time,explicit_skip||natural_transition);
}

void DecoderOwnershipTracker::ObservationLost(ObservationLoss,double time) {
    (void)time;
    for(auto& capture:captures_) {
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

std::uint64_t DecoderOwnershipTracker::OwnerToken(std::uintptr_t owner) const noexcept {
    for(const auto& record:owners_)if(record.token&&record.owner==owner)return record.token;
    return 0;
}

std::uint64_t DecoderOwnershipTracker::PlaybackToken(const std::string& playback_id) const noexcept {
    for(const auto& record:playbacks_)if(record.token&&record.id==playback_id)return record.token;
    return 0;
}

} // namespace ownership
