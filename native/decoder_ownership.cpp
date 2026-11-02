#include "decoder_ownership.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ownership {
namespace {

constexpr double kRetentionSeconds=10.0;

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

void DecoderOwnershipTracker::InvalidateOwner(std::uintptr_t owner) noexcept {
    for(auto& capture:captures_) {
        if(capture.token&&capture.owner==owner)capture.state=OwnershipState::Invalidated;
    }
    const auto current=FindCapture(current_capture_);
    if(current&&current->owner==owner)current_capture_=current_playback_=0;
    const auto pending=FindCapture(pending_capture_);
    if(pending&&pending->owner==owner)pending_capture_=0;
}

void DecoderOwnershipTracker::InvalidatePlayback(std::uint64_t playback) noexcept {
    if(!playback)return;
    for(auto& capture:captures_) {
        if(capture.token&&capture.playback_token==playback)
            capture.state=OwnershipState::Invalidated;
    }
    if(current_playback_==playback)current_playback_=0;
    if(latest_playback_==playback)latest_playback_=0;
}

DecoderOwnershipTracker::OwnerRecord&
DecoderOwnershipTracker::EnsureOwner(std::uintptr_t owner,unsigned epoch) {
    if(auto* existing=FindOwner(owner))return *existing;
    auto* slot=EmptyOrOldest(owners_);
    if(slot->token)InvalidateOwner(slot->owner);
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
    if(slot->token)InvalidatePlayback(slot->token);
    *slot={};slot->id=id;slot->uri=uri;slot->token=next_playback_++;
    slot->sequence=sequence_++;slot->listen_generation=generation;
    slot->first_observed=observed;slot->observed=observed;
    return *slot;
}

void DecoderOwnershipTracker::Expire(double now) noexcept {
    if(!ValidTime(now))return;
    for(auto& capture:captures_) {
        if(!capture.token||capture.state==OwnershipState::Bound||
           capture.state==OwnershipState::Conflict||capture.state==OwnershipState::Invalidated)
            continue;
        const auto since=capture.first_output?capture.first_output:capture.begun;
        if(now>since&&now-since>kRetentionSeconds)
            capture.state=OwnershipState::Invalidated;
    }
    if(const auto* pending=FindCapture(pending_capture_);
       pending&&pending->state==OwnershipState::Invalidated)pending_capture_=0;
    if(const auto* current=FindCapture(current_capture_);
       current&&current->state==OwnershipState::Invalidated) {
        current_capture_=0;current_playback_=0;
    }
}

std::uint64_t DecoderOwnershipTracker::BeginCapture(DecoderCodec codec,std::uintptr_t owner,
    std::uint32_t stream,double time,unsigned epoch) {
    Expire(time);
    auto* slot=EmptyOrOldest(captures_);
    if(slot->token) {
        if(current_capture_==slot->token)current_capture_=current_playback_=0;
        if(pending_capture_==slot->token)pending_capture_=0;
    }
    *slot={};slot->token=next_capture_++;slot->sequence=sequence_++;slot->codec=codec;
    slot->owner=owner;slot->stream=stream;slot->begun=time;slot->epoch=epoch;slot->active=true;
    if(!owner||epoch!=epoch_||!ValidTime(time)) {
        slot->state=OwnershipState::Invalidated;
    } else {
        slot->owner_token=EnsureOwner(owner,epoch).token;
    }
    return slot->token;
}

void DecoderOwnershipTracker::CompleteCapture(std::uint64_t capture_token,double time) {
    Expire(time);auto* capture=FindCapture(capture_token);if(!capture)return;
    if(!ValidTime(time)||time<=capture->begun)capture->state=OwnershipState::Conflict;
    capture->active=false;capture->complete=true;capture->completed_at=time;
}

void DecoderOwnershipTracker::AbortCapture(std::uint64_t capture_token,CaptureAbortReason) {
    auto* capture=FindCapture(capture_token);if(!capture)return;
    capture->active=false;capture->complete=false;capture->state=OwnershipState::Invalidated;
    if(current_capture_==capture_token)current_capture_=current_playback_=0;
    if(pending_capture_==capture_token)pending_capture_=0;
}

void DecoderOwnershipTracker::Conflict(CaptureRecord& capture) noexcept {
    if(capture.state!=OwnershipState::Invalidated)capture.state=OwnershipState::Conflict;
    if(pending_capture_==capture.token)pending_capture_=0;
}

void DecoderOwnershipTracker::ObserveDecoded(const DecodedActivity& activity) {
    Expire(activity.last);
    if(!activity.owner||!activity.calls||!activity.produced_floats)return;
    auto* owner=FindOwner(activity.owner);
    if(!owner||activity.epoch!=epoch_||owner->epoch!=activity.epoch) {
        InvalidateOwner(activity.owner);return;
    }
    CaptureRecord* candidate=nullptr;unsigned live=0;
    for(auto& capture:captures_) {
        if(capture.token&&capture.owner==activity.owner&&capture.active&&
           capture.state!=OwnershipState::Invalidated) {
            candidate=&capture;++live;
        }
    }
    if(!live)return;
    if(!ValidTime(activity.first)||!ValidTime(activity.last)||activity.last<activity.first) {
        for(auto& capture:captures_)if(capture.token&&capture.owner==activity.owner&&capture.active)
            Conflict(capture);
        return;
    }
    // GetTickCount64 is millisecond-granular. Distinct, structurally normalized
    // decoder owners routinely overlap on the same tick during preload; owner
    // identity and capture lifecycle disambiguate them. Only regression loses
    // the event order needed by this state machine.
    if(latest_decoded_time_&&activity.first<latest_decoded_time_) {
        for(auto& capture:captures_)if(capture.token&&capture.owner==activity.owner&&capture.active)
            Conflict(capture);
        return;
    }
    if(owner->last&&activity.first<owner->last) {
        for(auto& capture:captures_)if(capture.token&&capture.owner==activity.owner&&capture.active)
            Conflict(capture);
        return;
    }
    owner->first=owner->first?owner->first:activity.first;owner->last=activity.last;
    latest_decoded_time_=activity.last;
    if(live!=1) {
        for(auto& capture:captures_)if(capture.token&&capture.owner==activity.owner&&capture.active)
            Conflict(capture);
        return;
    }
    if(candidate->state==OwnershipState::Conflict)return;
    const auto was_bound=candidate->state==OwnershipState::Bound;
    // Capture-begin and the first decoded callback use a millisecond boot clock.
    // Equality is routine for this already-proven owner edge; only a genuinely
    // earlier decoded observation contradicts the lifecycle.
    if(activity.first<candidate->begun) {Conflict(*candidate);return;}
    candidate->first_output=candidate->first_output?candidate->first_output:activity.first;
    candidate->last_output=activity.last;
    if(was_bound)return;
    candidate->state=OwnershipState::AwaitingPlaybackInstance;

    auto* current=FindCapture(current_capture_);
    if(!current_capture_||!current||current->state==OwnershipState::Invalidated) {
        current_capture_=candidate->token;
    } else if(current->token!=candidate->token) {
        if(current->state!=OwnershipState::Bound||!current_playback_) {
            Conflict(*current);Conflict(*candidate);return;
        }
        if(pending_capture_&&pending_capture_!=candidate->token) {
            if(auto* pending=FindCapture(pending_capture_))Conflict(*pending);
            Conflict(*candidate);return;
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
    if(!ValidTime(observed)) {Conflict(capture);return;}
    // A transition snapshot can predate the first decoded callback for its new
    // owner and be drained afterward. It is not binding evidence yet; the next
    // fresh Ready observation must establish the ordering. Exact equality is
    // still ambiguous across queues and remains a conflict.
    if(observed<capture.first_output)return;
    if(observed==capture.first_output) {Conflict(capture);return;}
    if(observed-capture.first_output>kRetentionSeconds) {
        capture.state=OwnershipState::Invalidated;return;
    }
    // A newly discovered decoder can belong to Spotify's preloaded successor.
    // A recent repeat observation does not make an old playback instance new.
    if(capture.first_output-playback.first_observed>kRetentionSeconds)return;
    if(pending_capture_==capture.token&&playback.token==current_playback_)return;
    capture.playback_token=playback.token;capture.listen_generation=playback.listen_generation;
    capture.state=OwnershipState::Bound;current_capture_=capture.token;
    current_playback_=playback.token;latest_playback_=playback.token;
    if(pending_capture_==capture.token)pending_capture_=0;
}

void DecoderOwnershipTracker::ObservePlaybackImpl(const std::string& playback_id,
    const std::string& uri,std::uint64_t listen_generation,double observed) {
    Expire(observed);if(playback_id.empty()||!ValidTime(observed))return;
    auto* prior=FindPlayback(playback_id);
    auto* latest=FindPlayback(latest_playback_);
    if(latest&&latest!=prior&&observed<=latest->observed) {
        if(auto* target=FindCapture(pending_capture_?pending_capture_:current_capture_))Conflict(*target);
        return;
    }
    if(prior&&observed<=prior->observed) {
        // The media collector can repeat one buffering transition with small
        // timestamp jitter. Same-ID stale observations provide no new evidence;
        // ignore them and require a later fresh observation to bind.
        return;
    }
    auto& playback=EnsurePlayback(playback_id,uri,listen_generation,observed);
    latest_playback_=playback.token;
    auto* target=FindCapture(pending_capture_?pending_capture_:current_capture_);
    if(!target)return;
    if(target->state==OwnershipState::Bound&&target->playback_token==playback.token&&
       listen_generation) {
        target->listen_generation=listen_generation;return;
    }
    TryBind(*target,playback,observed);
}

void DecoderOwnershipTracker::ObservePlayback(const std::string& playback_id,
    const std::string& uri,std::uint64_t listen_generation,double observed) {
    ObservePlaybackImpl(playback_id,uri,listen_generation,observed);
}

void DecoderOwnershipTracker::ObserveTransition(const std::string& playback_id,
    const std::string& uri,double transition_time,bool explicit_skip,bool natural_transition) {
    (void)explicit_skip;(void)natural_transition;
    ObservePlaybackImpl(playback_id,uri,0,transition_time);
}

void DecoderOwnershipTracker::ObservationLost(ObservationLoss,double time) {
    Expire(time);
    for(auto& capture:captures_) {
        if(capture.token&&(capture.active||capture.complete))
            capture.state=OwnershipState::Invalidated;
    }
    current_capture_=pending_capture_=current_playback_=latest_playback_=0;
}

void DecoderOwnershipTracker::Reset(unsigned epoch) {
    captures_={};owners_={};playbacks_={};
    current_capture_=pending_capture_=current_playback_=latest_playback_=0;
    latest_decoded_time_=0;
    epoch_=epoch;
}

OwnershipBinding DecoderOwnershipTracker::Binding(std::uint64_t capture_token) const {
    OwnershipBinding binding{};binding.capture_token=capture_token;
    const auto* capture=FindCapture(capture_token);
    if(!capture) {binding.state=OwnershipState::Invalidated;return binding;}
    binding.playback_token=capture->playback_token;
    binding.owner_token=capture->owner_token;binding.listen_generation=capture->listen_generation;
    binding.state=capture->state;
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
