#include "../native/decoder_ownership.h"

#include <cassert>
#include <cstdint>
#include <limits>
#include <string>

namespace {

using ownership::CaptureAbortReason;
using ownership::DecodedActivity;
using ownership::DecoderCodec;
using ownership::DecoderOwnershipTracker;
using ownership::ObservationLoss;
using ownership::OwnershipBinding;
using ownership::OwnershipState;

DecodedActivity Activity(std::uintptr_t owner,double first,double last,unsigned epoch=1) {
    DecodedActivity activity{};
    activity.owner=owner;activity.calls=1;activity.produced_floats=4096;
    activity.consumed_bytes=1024;activity.kept_floats=4096;
    activity.first=first;activity.last=last;activity.epoch=epoch;
    return activity;
}

OwnershipBinding State(const DecoderOwnershipTracker& tracker,std::uint64_t token,
                       OwnershipState expected) {
    const auto binding=tracker.Binding(token);
    assert(binding.capture_token==token);assert(binding.state==expected);
    return binding;
}

void TestNormalization() {
    std::uintptr_t owner=123;
    assert(!ownership::NormalizeDecoderOwner(0,0,owner)&&owner==0);
    assert(ownership::NormalizeDecoderOwner(0x1000,0x80,owner)&&owner==0x1080);
    assert(ownership::NormalizeDecoderOwner(0x1088,-0x88,owner)&&owner==0x1000);
    assert(!ownership::NormalizeDecoderOwner(0x80,-0x88,owner)&&owner==0);
    const auto maximum=std::numeric_limits<std::uintptr_t>::max();
    assert(!ownership::NormalizeDecoderOwner(maximum-7,8,owner)&&owner==0);
    assert(!ownership::NormalizeDecoderOwner(8,-8,owner)&&owner==0);
    assert(!ownership::NormalizeDecoderOwner(0x1000,1,owner)&&owner==0);
}

void TestSingleOwnerBinds() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto capture=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.0,1);
    assert(capture);State(tracker,capture,OwnershipState::AwaitingOutput);
    tracker.ObserveDecoded(Activity(0x1000,1.0,1.0));
    State(tracker,capture,OwnershipState::AwaitingPlaybackInstance);
    tracker.Reset(1);
    const auto restarted=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.0,1);
    auto empty=Activity(0x1000,1.05,1.05);empty.produced_floats=0;
    tracker.ObserveDecoded(empty);
    State(tracker,restarted,OwnershipState::AwaitingOutput);
    tracker.ObserveDecoded(Activity(0x1000,1.1,1.2));
    State(tracker,restarted,OwnershipState::AwaitingPlaybackInstance);
    tracker.ObserveDecoded(Activity(0x1000,1.2,1.2));
    State(tracker,restarted,OwnershipState::AwaitingPlaybackInstance);
    tracker.ObservePlayback("play-1","uri:one",7,1.3);
    const auto binding=State(tracker,restarted,OwnershipState::Bound);
    assert(binding.owner_token&&binding.playback_token&&binding.listen_generation==7);
    assert(tracker.OwnerToken(0x1000)==binding.owner_token&&
           tracker.PlaybackToken("play-1")==binding.playback_token);
    tracker.ObserveDecoded(Activity(0x1000,1.4,1.5));
    const auto continued=State(tracker,restarted,OwnershipState::Bound);
    assert(continued.playback_token==binding.playback_token);
    tracker.CompleteCapture(restarted,4.0);
    State(tracker,restarted,OwnershipState::Bound);
}

void TestSameSongRepeatNeedsNewInstance() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto first=tracker.BeginCapture(DecoderCodec::Ogg,0x1000,41,1.0,1);
    tracker.ObserveDecoded(Activity(0x1000,1.1,1.2));
    tracker.ObservePlayback("instance-a","uri:same",1,1.3);
    const auto first_binding=State(tracker,first,OwnershipState::Bound);
    tracker.CompleteCapture(first,2.0);

    const auto second=tracker.BeginCapture(DecoderCodec::Ogg,0x2000,42,2.1,1);
    tracker.ObserveDecoded(Activity(0x2000,2.2,2.3));
    tracker.ObservePlayback("instance-a","uri:same",2,2.4);
    State(tracker,second,OwnershipState::AwaitingPlaybackInstance);
    tracker.ObservePlayback("instance-b","uri:same",2,2.5);
    const auto second_binding=State(tracker,second,OwnershipState::Bound);
    assert(second_binding.playback_token!=first_binding.playback_token);
}

void TestPreloadedSuccessorWaitsForNewPlaybackInstance() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);

    const auto outgoing=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.0,1);
    tracker.ObserveDecoded(Activity(0x1000,1.1,1.2));
    tracker.ObservePlayback("instance-old","uri:old",1,1.3);
    State(tracker,outgoing,OwnershipState::Bound);
    tracker.AbortCapture(outgoing,CaptureAbortReason::SettingsChange);

    tracker.ObservePlayback("instance-old","uri:old",1,130.0);
    const auto preload=tracker.BeginCapture(DecoderCodec::Flac,0x2000,0,130.1,1);
    tracker.ObserveDecoded(Activity(0x2000,130.1,130.1));
    tracker.ObservePlayback("instance-old","uri:old",1,130.2);
    State(tracker,preload,OwnershipState::AwaitingPlaybackInstance);

    tracker.ObservePlayback("instance-new","uri:new",2,133.0);
    const auto binding=State(tracker,preload,OwnershipState::Bound);
    assert(binding.playback_token==tracker.PlaybackToken("instance-new"));
    assert(binding.listen_generation==2);
}

void TestUnavailableInstanceAndPauseResume() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto capture=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.0,1);
    tracker.ObserveDecoded(Activity(0x1000,1.1,1.2));
    tracker.ObservePlayback("","uri:one",1,1.3);
    State(tracker,capture,OwnershipState::AwaitingPlaybackInstance);
    tracker.ObservePlayback("instance-a","uri:one",1,1.4);
    const auto bound=State(tracker,capture,OwnershipState::Bound);
    tracker.ObservePlayback("instance-a","uri:one",1,5.0);
    const auto resumed=State(tracker,capture,OwnershipState::Bound);
    assert(resumed.playback_token==bound.playback_token);
}

void TestSuccessorsAndSkips() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto outgoing=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.0,1);
    tracker.ObserveDecoded(Activity(0x1000,1.1,1.2));
    tracker.ObservePlayback("old","uri:old",1,1.3);
    State(tracker,outgoing,OwnershipState::Bound);

    tracker.ObserveDecoded(Activity(0x1000,2.0,2.0));
    const auto successor=tracker.BeginCapture(DecoderCodec::Flac,0x2000,0,2.0,1);
    tracker.ObserveDecoded(Activity(0x2000,2.0,2.0));
    State(tracker,outgoing,OwnershipState::Bound);
    State(tracker,successor,OwnershipState::AwaitingPlaybackInstance);
    tracker.ObserveTransition("new","uri:new",2.3,true,false);
    const auto skipped=State(tracker,successor,OwnershipState::Bound);
    assert(skipped.playback_token);

    const auto second=tracker.BeginCapture(DecoderCodec::Flac,0x3000,0,3.0,1);
    const auto third=tracker.BeginCapture(DecoderCodec::Flac,0x4000,0,3.0,1);
    tracker.ObserveDecoded(Activity(0x3000,3.1,3.2));
    tracker.ObserveDecoded(Activity(0x4000,3.3,3.4));
    State(tracker,second,OwnershipState::Conflict);
    State(tracker,third,OwnershipState::Conflict);

    tracker.Reset(3);
    const auto delayed=tracker.BeginCapture(DecoderCodec::Flac,0x7000,0,5.0,3);
    tracker.ObserveDecoded(Activity(0x7000,5.1,5.2,3));
    tracker.ObserveTransition("early","uri:early",5.05,false,true);
    State(tracker,delayed,OwnershipState::AwaitingPlaybackInstance);
    tracker.ObserveTransition("early","uri:early",5.04,false,true);
    State(tracker,delayed,OwnershipState::AwaitingPlaybackInstance);
    tracker.ObservePlayback("early","uri:early",8,5.3);
    State(tracker,delayed,OwnershipState::Bound);

    tracker.Reset(2);
    const auto unknown_first=tracker.BeginCapture(DecoderCodec::Flac,0x5000,0,4.0,2);
    const auto unknown_second=tracker.BeginCapture(DecoderCodec::Flac,0x6000,0,4.0,2);
    tracker.ObserveDecoded(Activity(0x5000,4.1,4.2,2));
    tracker.ObserveDecoded(Activity(0x6000,4.3,4.4,2));
    State(tracker,unknown_first,OwnershipState::Conflict);
    State(tracker,unknown_second,OwnershipState::Conflict);
}

void TestCaptureConflictsAndReuse() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto first=tracker.BeginCapture(DecoderCodec::Ogg,0x1000,41,1.0,1);
    const auto second=tracker.BeginCapture(DecoderCodec::Ogg,0x1000,42,1.0,1);
    tracker.ObserveDecoded(Activity(0x1000,1.1,1.2));
    State(tracker,first,OwnershipState::Conflict);
    State(tracker,second,OwnershipState::Conflict);

    tracker.Reset(2);
    const auto old=tracker.BeginCapture(DecoderCodec::Flac,0x2000,0,2.0,2);
    tracker.ObserveDecoded(Activity(0x2000,2.1,2.2,2));
    tracker.ObservePlayback("old","uri:old",1,2.3);
    State(tracker,old,OwnershipState::Bound);
    tracker.CompleteCapture(old,2.4);
    const auto fresh=tracker.BeginCapture(DecoderCodec::Flac,0x2000,0,3.0,2);
    assert(fresh!=old);tracker.ObserveDecoded(Activity(0x2000,3.1,3.2,2));
    tracker.ObservePlayback("new","uri:new",2,3.3);
    State(tracker,fresh,OwnershipState::Bound);
    State(tracker,old,OwnershipState::Bound);
}

void TestAbortLossEpochAndTime() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto aborted=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.0,1);
    tracker.AbortCapture(aborted,CaptureAbortReason::DecoderError);
    tracker.ObserveDecoded(Activity(0x1000,1.1,1.2));
    State(tracker,aborted,OwnershipState::Invalidated);

    const auto lost=tracker.BeginCapture(DecoderCodec::Flac,0x2000,0,2.0,1);
    tracker.ObserveDecoded(Activity(0x2000,2.1,2.2));
    tracker.ObservationLost(ObservationLoss::Media,2.3);
    tracker.ObservePlayback("after-loss","uri:x",1,2.4);
    State(tracker,lost,OwnershipState::Invalidated);

    const auto old_epoch=tracker.BeginCapture(DecoderCodec::Flac,0x3000,0,3.0,1);
    tracker.Reset(2);
    State(tracker,old_epoch,OwnershipState::Invalidated);
    const auto wrong_epoch=tracker.BeginCapture(DecoderCodec::Flac,0x4000,0,4.0,1);
    State(tracker,wrong_epoch,OwnershipState::Invalidated);

    const auto equal=tracker.BeginCapture(DecoderCodec::Flac,0x5000,0,5.0,2);
    tracker.ObserveDecoded(Activity(0x5000,5.1,5.1,2));
    State(tracker,equal,OwnershipState::AwaitingPlaybackInstance);
    tracker.ObservePlayback("equal-output","uri:equal",3,5.1);
    State(tracker,equal,OwnershipState::Conflict);

    const auto regressed=tracker.BeginCapture(DecoderCodec::Flac,0x6000,0,6.0,2);
    tracker.ObserveDecoded(Activity(0x6000,6.2,6.1,2));
    State(tracker,regressed,OwnershipState::Conflict);

    const auto playback_equal=tracker.BeginCapture(DecoderCodec::Flac,0x7000,0,7.0,2);
    tracker.ObserveDecoded(Activity(0x7000,7.1,7.2,2));
    tracker.ObservePlayback("equal","uri:e",3,7.1);
    State(tracker,playback_equal,OwnershipState::Conflict);

    tracker.Reset(3);
    const auto ordered=tracker.BeginCapture(DecoderCodec::Flac,0x8000,0,8.0,3);
    tracker.ObserveDecoded(Activity(0x8000,8.1,8.2,3));
    tracker.ObservePlayback("ordered","uri:o",4,8.3);
    State(tracker,ordered,OwnershipState::Bound);
    const auto regressed_transition=tracker.BeginCapture(DecoderCodec::Flac,0x9000,0,8.35,3);
    tracker.ObserveDecoded(Activity(0x9000,8.4,8.5,3));
    tracker.ObserveTransition("regressed","uri:r",8.3,false,true);
    State(tracker,regressed_transition,OwnershipState::Conflict);

    tracker.Reset(4);
    const auto decoded_first=tracker.BeginCapture(DecoderCodec::Flac,0xa000,0,9.0,4);
    tracker.ObserveDecoded(Activity(0xa000,9.1,9.2,4));
    const auto decoded_regression=tracker.BeginCapture(DecoderCodec::Flac,0xb000,0,9.0,4);
    tracker.ObserveDecoded(Activity(0xb000,9.15,9.3,4));
    State(tracker,decoded_first,OwnershipState::AwaitingPlaybackInstance);
    State(tracker,decoded_regression,OwnershipState::Conflict);
}

void TestRetentionNeverChooses() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto capture=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.0,1);
    tracker.ObserveDecoded(Activity(0x1000,1.1,1.2));
    tracker.ObservePlayback("too-late","uri:late",1,11.2001);
    State(tracker,capture,OwnershipState::Invalidated);
}

void TestBoundedCaptureAndOwnerStorage() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    std::uint64_t first_capture=0;
    for(unsigned index=0;index<33;++index) {
        const auto token=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,double(index),1);
        if(!index)first_capture=token;
    }
    State(tracker,first_capture,OwnershipState::Invalidated);

    tracker.Reset(2);first_capture=0;
    for(unsigned index=0;index<17;++index) {
        const auto owner=std::uintptr_t(0x1000+index*0x100);
        const auto capture=tracker.BeginCapture(DecoderCodec::Flac,owner,0,index+1.0,2);
        if(!index)first_capture=capture;
        tracker.ObserveDecoded(Activity(owner,index+1.1,index+1.2,2));
    }
    State(tracker,first_capture,OwnershipState::Invalidated);
}

void TestBoundedPlaybackStorage() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto capture=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.0,1);
    tracker.ObserveDecoded(Activity(0x1000,1.1,1.2));
    tracker.ObservePlayback("play-0","uri:0",1,1.3);
    State(tracker,capture,OwnershipState::Bound);
    for(unsigned index=1;index<17;++index)
        tracker.ObservePlayback("play-"+std::to_string(index),"uri:x",index+1,2.0+index);
    State(tracker,capture,OwnershipState::Invalidated);
}

} // namespace

int main() {
    TestNormalization();
    TestSingleOwnerBinds();
    TestSameSongRepeatNeedsNewInstance();
    TestPreloadedSuccessorWaitsForNewPlaybackInstance();
    TestUnavailableInstanceAndPauseResume();
    TestSuccessorsAndSkips();
    TestCaptureConflictsAndReuse();
    TestAbortLossEpochAndTime();
    TestRetentionNeverChooses();
    TestBoundedCaptureAndOwnerStorage();
    TestBoundedPlaybackStorage();
}
