#include "../native/decoder_ownership.h"

#include <cassert>
#include <cstdio>
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
using ownership::OwnershipReason;
using ownership::OwnershipState;

DecodedActivity Activity(std::uintptr_t owner,double first,double last,unsigned epoch=1,
                         std::uint64_t sequence=0) {
    DecodedActivity activity{};
    activity.owner=owner;activity.calls=1;activity.produced_floats=4096;
    activity.consumed_bytes=1024;activity.kept_floats=4096;
    activity.first=first;activity.last=last;activity.epoch=epoch;activity.sequence=sequence;
    return activity;
}

OwnershipBinding State(const DecoderOwnershipTracker& tracker,std::uint64_t token,
                       OwnershipState expected) {
    const auto binding=tracker.Binding(token);
    if(binding.state!=expected)std::fprintf(stderr,"token=%llu expected=%u actual=%u caller=%p\n",
        static_cast<unsigned long long>(token),unsigned(expected),unsigned(binding.state),
        __builtin_return_address(0));
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
    assert(capture);assert(State(tracker,capture,OwnershipState::AwaitingOutput).reason==
                           OwnershipReason::CaptureBegan);
    tracker.ObserveDecoded(Activity(0x1000,1.0,1.0));
    assert(State(tracker,capture,OwnershipState::AwaitingPlaybackInstance).reason==
           OwnershipReason::DecodedOutputObserved);
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
    assert(binding.reason==OwnershipReason::PlaybackStageBound);
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

void TestPreloadedSuccessorAllowsConcurrentOutgoingOutput() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto outgoing=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.0,1);
    tracker.ObserveDecoded(Activity(0x1000,1.1,1.2));
    tracker.ObservePlayback("instance-old","uri:old",1,1.3);
    State(tracker,outgoing,OwnershipState::Bound);

    const auto preload=tracker.BeginCapture(DecoderCodec::Flac,0x2000,0,9.0,1);
    tracker.ObserveDecoded(Activity(0x2000,9.2,9.3));
    State(tracker,preload,OwnershipState::AwaitingPlaybackInstance);

    // Decoder callbacks from the outgoing stream and its preloaded successor
    // can overlap. Cross-owner timestamp order is not ownership evidence.
    tracker.ObserveDecoded(Activity(0x1000,9.1,9.25));
    assert(tracker.Binding(outgoing).state==OwnershipState::Bound);
    assert(tracker.Binding(preload).state==OwnershipState::AwaitingPlaybackInstance);

    tracker.ObserveTransition("","",9.35,false,true);
    assert(tracker.Binding(outgoing).state==OwnershipState::Bound);
    tracker.ObservePlayback("instance-new","uri:new",2,9.4);
    const auto binding=State(tracker,preload,OwnershipState::Bound);
    assert(binding.playback_token==tracker.PlaybackToken("instance-new"));
    assert(binding.listen_generation==2);
}

void TestMixedVoiceAndMusicSelectByPlaybackKind() {
    {
        DecoderOwnershipTracker tracker;tracker.Reset(1);
        const auto voice=tracker.BeginCapture(DecoderCodec::Mp3,0x1000,0,1.0,1);
        tracker.ObserveDecoded(Activity(0x1000,1.0,1.1));
        const auto music=tracker.BeginCapture(DecoderCodec::Flac,0x2000,0,1.02,1);
        tracker.ObserveDecoded(Activity(0x2000,1.02,1.12));
        State(tracker,voice,OwnershipState::AwaitingPlaybackInstance);
        State(tracker,music,OwnershipState::AwaitingPlaybackInstance);

        tracker.ObservePlayback("music","spotify:track:0000000000000000000001",0,1.2);
        State(tracker,voice,OwnershipState::AwaitingPlaybackInstance);
        auto binding=State(tracker,music,OwnershipState::Bound);
        assert(binding.listen_generation==0);
        tracker.ObservePlayback("music","spotify:track:0000000000000000000001",7,1.2);
        binding=State(tracker,music,OwnershipState::Bound);
        assert(binding.listen_generation==7&&
               "the same Ready observation can attach the new listen generation");
    }
    {
        DecoderOwnershipTracker tracker;tracker.Reset(1);
        const auto voice=tracker.BeginCapture(DecoderCodec::Mp3,0x3000,0,2.0,1);
        tracker.ObserveDecoded(Activity(0x3000,2.0,2.1));
        const auto preload=tracker.BeginCapture(DecoderCodec::Flac,0x4000,0,2.02,1);
        tracker.ObserveDecoded(Activity(0x4000,2.02,2.12));
        tracker.ObservePlayback("episode","spotify:episode:0000000000000000000001",8,2.2);
        State(tracker,voice,OwnershipState::Bound);
        State(tracker,preload,OwnershipState::AwaitingPlaybackInstance);
    }
    {
        DecoderOwnershipTracker tracker;tracker.Reset(1);
        const auto voice=tracker.BeginCapture(DecoderCodec::Mp3,0x5000,0,3.0,1);
        tracker.ObserveDecoded(Activity(0x5000,3.0,3.1));
        tracker.ObservePlayback("track","spotify:track:0000000000000000000001",9,3.2);
        State(tracker,voice,OwnershipState::AwaitingPlaybackInstance);
    }
    {
        DecoderOwnershipTracker tracker;tracker.Reset(1);
        const auto music=tracker.BeginCapture(DecoderCodec::Flac,0x6000,0,4.0,1);
        tracker.ObserveDecoded(Activity(0x6000,4.0,4.1));
        tracker.ObservePlayback("episode","spotify:episode:0000000000000000000001",10,4.2);
        State(tracker,music,OwnershipState::AwaitingPlaybackInstance);
    }
}

void TestUnavailableInstanceAndPauseResume() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto capture=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.0,1);
    tracker.ObserveDecoded(Activity(0x1000,1.1,1.2));
    tracker.ObservePlayback("","uri:one",1,1.3);
    const auto fallback=State(tracker,capture,OwnershipState::Bound);
    assert(fallback.listen_generation==1&&fallback.playback_token);
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
    State(tracker,successor,OwnershipState::AwaitingPlaybackInstance);
    tracker.ObservePlayback("new","uri:new",2,2.4);
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

void TestCompletedConflictDoesNotPoisonLaterCapture() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto first=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.0,1);
    const auto second=tracker.BeginCapture(DecoderCodec::Flac,0x2000,0,1.0,1);
    tracker.ObserveDecoded(Activity(0x1000,1.1,1.2));
    tracker.ObserveDecoded(Activity(0x2000,1.3,1.4));
    State(tracker,first,OwnershipState::Conflict);
    State(tracker,second,OwnershipState::Conflict);
    tracker.CompleteCapture(first,2.0);
    tracker.CompleteCapture(second,2.0);

    const auto fresh=tracker.BeginCapture(DecoderCodec::Flac,0x3000,0,102.0,1);
    tracker.ObserveDecoded(Activity(0x3000,102.1,102.2));
    tracker.ObservePlayback("fresh","uri:fresh",3,102.3);
    const auto binding=State(tracker,fresh,OwnershipState::Bound);
    assert(binding.listen_generation==3);
}

void TestDelayedDecodedEvidenceUsesCompletedLifetimeWindow() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto first=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.0,1);
    tracker.CompleteCapture(first,2.0);
    const auto reused=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,3.0,1);
    tracker.CompleteCapture(reused,4.0);

    tracker.ObserveDecoded(Activity(0x1000,1.1,1.9));
    State(tracker,first,OwnershipState::AwaitingPlaybackInstance);
    State(tracker,reused,OwnershipState::AwaitingOutput);
    tracker.ObservePlayback("first-life","uri:first",1,2.1);
    State(tracker,first,OwnershipState::Bound);

    tracker.ObserveDecoded(Activity(0x1000,3.1,3.9));
    State(tracker,reused,OwnershipState::AwaitingPlaybackInstance);
    tracker.ObservePlayback("second-life","uri:second",2,4.1);
    const auto second_binding=State(tracker,reused,OwnershipState::Bound);
    assert(second_binding.playback_token==tracker.PlaybackToken("second-life"));
    assert(second_binding.listen_generation==2);
}

void TestProducerSequenceOwnsCompletionBoundaryAndAddressReuse() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto first=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.0,1,10);
    tracker.CompleteCapture(first,2.0,20);
    tracker.ObserveDecoded(Activity(0x1000,1.9,2.001,1,21));
    State(tracker,first,OwnershipState::AwaitingPlaybackInstance);

    const auto reused=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,3.0,1,30);
    tracker.CompleteCapture(reused,4.0,40);
    tracker.ObserveDecoded(Activity(0x1000,3.9,4.001,1,41));
    State(tracker,first,OwnershipState::AwaitingPlaybackInstance);
    State(tracker,reused,OwnershipState::AwaitingPlaybackInstance);
}

void TestProducerSequenceSeparatesStillActiveReusedDecoder() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto first=tracker.BeginCapture(DecoderCodec::Ogg,0x1000,41,1.0,1,10);
    tracker.ObserveDecoded(Activity(0x1000,1.1,1.2,1,11));
    assert(tracker.BindCaptureExact(first,"old","uri:old",1,1.3));

    // The compressed reader can retain the old stream while the same decoder
    // starts its successor. A delayed callback still belongs before that begin.
    const auto reused=tracker.BeginCapture(DecoderCodec::Ogg,0x1000,42,2.0,1,20);
    tracker.ObserveDecoded(Activity(0x1000,1.9,1.9,1,19));
    State(tracker,first,OwnershipState::Bound);
    State(tracker,reused,OwnershipState::AwaitingOutput);
    tracker.ObserveDecoded(Activity(0x1000,2.0,2.0,1,21));
    State(tracker,first,OwnershipState::Bound);
    State(tracker,reused,OwnershipState::AwaitingPlaybackInstance);
    assert(tracker.BindCaptureExact(reused,"new","uri:new",2,2.1));
    assert(tracker.Binding(reused).playback_token!=tracker.Binding(first).playback_token);
    assert(!tracker.ConflictEvidence(first)&&!tracker.ConflictEvidence(reused));

    // Completion can precede the outer decoded callback. The active older
    // reader must not steal output from the latest completed decoder lifetime.
    const auto completed=tracker.BeginCapture(DecoderCodec::Ogg,0x1000,43,3.0,1,30);
    tracker.CompleteCapture(completed,3.1,31);
    tracker.ObserveDecoded(Activity(0x1000,3.1,3.101,1,32));
    State(tracker,completed,OwnershipState::AwaitingPlaybackInstance);
    assert(tracker.BindCaptureExact(completed,"last","uri:last",3,3.2));
    State(tracker,first,OwnershipState::Bound);
    State(tracker,reused,OwnershipState::Bound);
}

void TestInvalidatedLatestLifetimeDoesNotFallBackToOlderReader() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto first=tracker.BeginCapture(DecoderCodec::Ogg,0x1000,41,1.0,1,10);
    const auto reused=tracker.BeginCapture(DecoderCodec::Ogg,0x1000,42,2.0,1,20);
    tracker.AbortCapture(reused,CaptureAbortReason::DecoderError);
    tracker.ObserveDecoded(Activity(0x1000,2.1,2.2,1,21));
    State(tracker,first,OwnershipState::AwaitingOutput);
    State(tracker,reused,OwnershipState::Invalidated);
    assert(!tracker.BindCaptureExact(first,"new","uri:new",1,2.3));
}

void TestDecodedRangeAcrossReusedDecoderRemainsAmbiguous() {
    for(const bool sequenced:{false,true}) {
        DecoderOwnershipTracker tracker;tracker.Reset(1);
        const auto first=tracker.BeginCapture(DecoderCodec::Ogg,0x1000,41,1.0,1,sequenced?10:0);
        tracker.ObserveDecoded(Activity(0x1000,1.1,1.2,1,sequenced?11:0));
        const auto second=tracker.BeginCapture(DecoderCodec::Ogg,0x1000,42,2.0,1,sequenced?20:0);
        auto crossing=Activity(0x1000,2.0,2.1,1,sequenced?19:0);
        crossing.last_sequence=sequenced?21:0;
        tracker.ObserveDecoded(crossing);
        for(const auto token:{first,second}) {
            assert(State(tracker,token,OwnershipState::Conflict).reason==
                   OwnershipReason::AmbiguousDecoderLifetime);
            const auto* evidence=tracker.ConflictEvidence(token);
            assert(evidence&&evidence->count==2);
            assert(evidence->captures[0]==first&&evidence->captures[1]==second);
            assert(evidence->activity.last_sequence==crossing.last_sequence);
            assert(!tracker.BindCaptureExact(token,"exact","uri:new",2,2.2)&&
                   "an exact decoder address cannot disambiguate overlapping streams");
        }
    }
}

void TestEqualProducerBeginsRemainAmbiguous() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto first=tracker.BeginCapture(DecoderCodec::Ogg,0x1000,41,1.0,1,10);
    const auto second=tracker.BeginCapture(DecoderCodec::Ogg,0x1000,42,1.0,1,10);
    tracker.ObserveDecoded(Activity(0x1000,1.1,1.2,1,11));
    State(tracker,first,OwnershipState::Conflict);
    State(tracker,second,OwnershipState::Conflict);
}

void TestAbortLossEpochAndTime() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto aborted=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.0,1);
    tracker.AbortCapture(aborted,CaptureAbortReason::DecoderError);
    tracker.ObserveDecoded(Activity(0x1000,1.1,1.2));
    assert(State(tracker,aborted,OwnershipState::Invalidated).reason==
           OwnershipReason::CaptureAborted);

    const auto lost=tracker.BeginCapture(DecoderCodec::Flac,0x2000,0,2.0,1);
    tracker.ObserveDecoded(Activity(0x2000,2.1,2.2));
    tracker.ObservationLost(ObservationLoss::Media,2.3);
    tracker.ObservePlayback("after-loss","uri:x",1,2.4);
    assert(State(tracker,lost,OwnershipState::Invalidated).reason==
           OwnershipReason::ObservationLost);

    const auto old_epoch=tracker.BeginCapture(DecoderCodec::Flac,0x3000,0,3.0,1);
    tracker.Reset(2);
    State(tracker,old_epoch,OwnershipState::Invalidated);
    const auto wrong_epoch=tracker.BeginCapture(DecoderCodec::Flac,0x4000,0,4.0,1);
    State(tracker,wrong_epoch,OwnershipState::Invalidated);

    const auto equal=tracker.BeginCapture(DecoderCodec::Flac,0x5000,0,5.0,2);
    tracker.ObserveDecoded(Activity(0x5000,5.1,5.1,2));
    State(tracker,equal,OwnershipState::AwaitingPlaybackInstance);
    tracker.ObservePlayback("equal-output","uri:equal",3,5.1);
    State(tracker,equal,OwnershipState::Bound);

    const auto regressed=tracker.BeginCapture(DecoderCodec::Flac,0x6000,0,6.0,2);
    tracker.ObserveDecoded(Activity(0x6000,6.2,6.1,2));
    State(tracker,regressed,OwnershipState::Conflict);

    const auto playback_equal=tracker.BeginCapture(DecoderCodec::Flac,0x7000,0,7.0,2);
    tracker.ObserveDecoded(Activity(0x7000,7.1,7.2,2));
    tracker.ObservePlayback("equal","uri:e",3,7.1);
    State(tracker,playback_equal,OwnershipState::Bound);

    tracker.Reset(3);
    const auto ordered=tracker.BeginCapture(DecoderCodec::Flac,0x8000,0,8.0,3);
    tracker.ObserveDecoded(Activity(0x8000,8.1,8.2,3));
    tracker.ObservePlayback("ordered","uri:o",4,8.3);
    State(tracker,ordered,OwnershipState::Bound);
    const auto regressed_transition=tracker.BeginCapture(DecoderCodec::Flac,0x9000,0,8.35,3);
    tracker.ObserveDecoded(Activity(0x9000,8.4,8.5,3));
    tracker.ObserveTransition("regressed","uri:r",8.3,false,true);
    State(tracker,regressed_transition,OwnershipState::AwaitingPlaybackInstance);
    tracker.ObservePlayback("regressed","uri:r",5,8.6);
    State(tracker,regressed_transition,OwnershipState::Bound);

    tracker.Reset(4);
    const auto decoded_first=tracker.BeginCapture(DecoderCodec::Flac,0xa000,0,9.0,4);
    tracker.ObserveDecoded(Activity(0xa000,9.1,9.2,4));
    const auto decoded_regression=tracker.BeginCapture(DecoderCodec::Flac,0xb000,0,9.0,4);
    tracker.ObserveDecoded(Activity(0xb000,9.15,9.3,4));
    State(tracker,decoded_first,OwnershipState::Conflict);
    State(tracker,decoded_regression,OwnershipState::Conflict);
}

void TestElapsedTimeNeverRejectsOwnershipEvidence() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto capture=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.0,1);
    tracker.ObserveDecoded(Activity(0x1000,1.1,1.2));
    tracker.ObservePlayback("", "uri:windows-only",0,50.0);
    State(tracker,capture,OwnershipState::AwaitingPlaybackInstance);
    tracker.CompleteCapture(capture,237.0);
    State(tracker,capture,OwnershipState::AwaitingPlaybackInstance);
}

void TestBlankIdOutgoingAndSuccessorUseLifecycleStages() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    tracker.ObserveTransition("outgoing","uri:old",1.0,false,false);
    const auto outgoing=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.1,1,10);
    tracker.ObserveDecoded(Activity(0x1000,1.2,1.3,1,11));
    tracker.ObservePlayback("","uri:old",1,1.4);
    State(tracker,outgoing,OwnershipState::Bound);

    // This is the live ordering: the next decoder starts while the outgoing
    // decoder is completing, and the Windows playback snapshot has no app ID.
    const auto successor=tracker.BeginCapture(DecoderCodec::Flac,0x2000,0,237.0,1,20);
    tracker.ObserveDecoded(Activity(0x2000,237.1,237.2,1,21));
    State(tracker,outgoing,OwnershipState::Bound);
    State(tracker,successor,OwnershipState::AwaitingPlaybackInstance);
    tracker.CompleteCapture(outgoing,237.3,22);

    // A repeated identity that predates the successor belongs to the outgoing
    // lifecycle. A genuinely new identity selects the successor lifecycle.
    tracker.ObservePlayback("outgoing","uri:old",1,237.4);
    State(tracker,outgoing,OwnershipState::Bound);
    State(tracker,successor,OwnershipState::AwaitingPlaybackInstance);
    tracker.ObserveTransition("successor","uri:new",237.5,false,true);
    State(tracker,successor,OwnershipState::AwaitingPlaybackInstance);

    // A buffering transition proves that the outgoing listen ended. It does
    // not identify the incoming listen and must never bind a decoder with
    // generation zero. The following Ready stage is the first point where the
    // media identity and listen generation are both established. Windows
    // fallback legitimately has no Spotify playback ID here.
    tracker.ObservePlayback("","uri:new",2,237.6);
    const auto bound=State(tracker,successor,OwnershipState::Bound);
    assert(bound.listen_generation==2);
}

void TestReadyClockSkewCannotRejectLifecycle() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto capture=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,10.0,1,10);
    tracker.ObserveDecoded(Activity(0x1000,10.1,10.2,1,11));

    // Provider clocks are not a common causal clock. A Ready snapshot drained
    // after decoded output may carry an earlier sample time. The consumed
    // lifecycle stage is binding evidence and must not poison the capture.
    tracker.ObservePlayback("instance","uri:one",1,10.0);
    State(tracker,capture,OwnershipState::Bound);
    tracker.ObservePlayback("instance","uri:one",1,10.3);
    const auto bound=State(tracker,capture,OwnershipState::Bound);
    assert(bound.listen_generation==1);
}

void TestCurrentItemChangeReconsidersOnlyUniqueAmbiguousSuccessor() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto outgoing=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.0,1,1);
    tracker.ObserveDecoded(Activity(0x1000,1.1,1.2,1,2));
    tracker.ObservePlayback("instance-a","spotify:track:0000000000000000000001",1,1.3);
    State(tracker,outgoing,OwnershipState::Bound);

    const auto voice=tracker.BeginCapture(DecoderCodec::Mp3,0x2000,0,2.0,1,3);
    tracker.ObserveDecoded(Activity(0x2000,2.1,2.2,1,4));
    const auto music=tracker.BeginCapture(DecoderCodec::Flac,0x3000,0,2.2,1,5);
    tracker.ObserveDecoded(Activity(0x3000,2.3,2.4,1,6));
    State(tracker,voice,OwnershipState::Conflict);
    State(tracker,music,OwnershipState::Conflict);
    tracker.AbortCapture(voice,CaptureAbortReason::InvalidData);
    assert(tracker.ReconsiderAmbiguousSuccessor(music));
    State(tracker,music,OwnershipState::AwaitingPlaybackInstance);
    tracker.ObservePlayback("instance-b","spotify:track:0000000000000000000002",2,2.5);
    assert(State(tracker,music,OwnershipState::Bound).listen_generation==2);

    tracker.Reset(2);
    const auto first=tracker.BeginCapture(DecoderCodec::Flac,0x4000,0,3.0,2,10);
    tracker.ObserveDecoded(Activity(0x4000,3.1,3.2,2,11));
    tracker.ObservePlayback("old","spotify:track:0000000000000000000003",3,3.3);
    State(tracker,first,OwnershipState::Bound);
    const auto second=tracker.BeginCapture(DecoderCodec::Flac,0x5000,0,4.0,2,12);
    tracker.ObserveDecoded(Activity(0x5000,4.1,4.2,2,13));
    const auto third=tracker.BeginCapture(DecoderCodec::Flac,0x6000,0,4.1,2,14);
    tracker.ObserveDecoded(Activity(0x6000,4.3,4.4,2,15));
    State(tracker,second,OwnershipState::Conflict);
    State(tracker,third,OwnershipState::Conflict);
    assert(!tracker.ReconsiderAmbiguousSuccessor(second)&&
           !tracker.ReconsiderAmbiguousSuccessor(third)&&
           "two viable music successors must remain ambiguous");

    tracker.Reset(3);
    const auto lost=tracker.BeginCapture(DecoderCodec::Flac,0x7000,0,5.0,3,20);
    tracker.ObserveDecoded(Activity(0x7000,5.1,5.2,3,21));
    tracker.ObservationLost(ObservationLoss::CaptureQueue,5.3);
    assert(!tracker.ReconsiderAmbiguousSuccessor(lost)&&
           "observation loss must remain terminal");
}

void TestExactDecoderIdentityResolvesSameCodecConflict() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto outgoing=tracker.BeginCapture(DecoderCodec::Flac,0x8000,0,0.5,1,1);
    tracker.ObserveDecoded(Activity(0x8000,0.6,0.7,1,2));
    tracker.ObservePlayback("outgoing","spotify:track:0000000000000000000098",98,0.8);
    State(tracker,outgoing,OwnershipState::Bound);
    const auto first=tracker.BeginCapture(DecoderCodec::Flac,0x8100,0,1.0,1,3);
    tracker.ObserveDecoded(Activity(0x8100,1.1,1.2,1,4));
    const auto second=tracker.BeginCapture(DecoderCodec::Flac,0x8200,0,1.2,1,5);
    tracker.ObserveDecoded(Activity(0x8200,1.3,1.4,1,6));
    State(tracker,first,OwnershipState::Conflict);
    State(tracker,second,OwnershipState::Conflict);

    assert(tracker.BindCaptureExact(second,"exact-playback",
        "spotify:track:0000000000000000000099",99,1.5));
    const auto selected=State(tracker,second,OwnershipState::Bound);
    assert(selected.listen_generation==99&&
           selected.playback_token==tracker.PlaybackToken("exact-playback"));
    State(tracker,first,OwnershipState::Conflict);
}

void TestExactCompletedSourceDoesNotRequireDecodedOutput() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    const auto source=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.0,1,10);
    assert(!tracker.BindCaptureExact(source,"exact","uri:source",1,1.1,true)&&
           "caller proof cannot replace tracker lifecycle completion");
    tracker.CompleteCapture(source,2.0,20);
    assert(!tracker.BindCaptureExact(source,"exact","uri:source",1,2.1)&&
           "complete lifecycle alone does not prove a validated source");
    assert(tracker.BindCaptureExact(source,"exact","uri:source",1,2.1,true));
    const auto binding=State(tracker,source,OwnershipState::Bound);
    assert(binding.listen_generation==1&&binding.playback_token==tracker.PlaybackToken("exact"));
    assert(!tracker.BindCaptureExact(source,"wrong","uri:wrong",2,2.2,true));
    assert(tracker.Binding(source).playback_token==binding.playback_token);
    assert(tracker.BindCaptureExact(source,"exact","uri:source",1,2.3,true));

    const auto other=tracker.BeginCapture(DecoderCodec::Flac,0x2000,0,3.0,1,30);
    tracker.CompleteCapture(other,4.0,40);
    assert(!tracker.BindCaptureExact(other,"exact","uri:source",1,4.1,true)&&
           "validated completion must preserve unique playback ownership");
    tracker.ObservationLost(ObservationLoss::DecodedQueue,4.2);
    assert(!tracker.BindCaptureExact(other,"fresh","uri:fresh",3,4.3,true)&&
           "completion proof must not revive lost ownership evidence");
}

void TestExactCompleteFlacCanReplaceCleanRestartChain() {
    DecoderOwnershipTracker tracker;tracker.Reset(1);
    auto capture=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.0,1,10);
    tracker.ObserveDecoded(Activity(0x1000,1.1,1.2,1,11));
    assert(tracker.BindCaptureExact(capture,"same","spotify:track:one",4,1.3));
    const auto playback=tracker.Binding(capture).playback_token;
    for(unsigned restart=0;restart<9;++restart) {
        tracker.AbortCapture(capture,CaptureAbortReason::Reinitialized);
        const double now=2.0+restart;
        capture=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,now,1,20+restart*10);
        tracker.ObserveDecoded(Activity(0x1000,now+.1,now+.2,1,21+restart*10));
        assert(!tracker.BindCaptureExact(capture,"same","spotify:track:one",4,now+.3));
    }
    tracker.CompleteCapture(capture,11.0,120);
    const char* rejection=nullptr;
    assert(!tracker.BindCaptureExact(capture,"same","spotify:track:one",4,11.1,false,&rejection));
    assert(rejection&&std::string(rejection)=="restart-source-not-complete");
    assert(!tracker.BindCapture(capture,"same","spotify:track:one",4,11.1));
    assert(tracker.BindCaptureExact(capture,"same","spotify:track:one",4,11.1,true));
    assert(State(tracker,capture,OwnershipState::Bound).playback_token==playback);
}

void TestExactRestartTransferRejectsUnsafePredecessors() {
    // Active, completed, wrong owner, lost evidence, conflict, and non-restart aborts.
    for(unsigned scenario=0;scenario<15;++scenario) {
        DecoderOwnershipTracker tracker;tracker.Reset(1);
        const auto first=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.0,1,10);
        tracker.ObserveDecoded(Activity(0x1000,1.1,1.2,1,11));
        assert(tracker.BindCaptureExact(first,"same","spotify:track:one",4,1.3));
        if(scenario==1)tracker.CompleteCapture(first,1.5,15);
        if(scenario==12) {
            auto invalid=Activity(0x1000,1.6,1.5,1,14);
            tracker.ObserveDecoded(invalid);
            State(tracker,first,OwnershipState::Conflict);
        }
        if(scenario==4)tracker.ObservationLost(ObservationLoss::CaptureQueue,1.5);
        if(scenario!=0)tracker.AbortCapture(first,scenario==5?
            CaptureAbortReason::QueueLoss:scenario==6?CaptureAbortReason::DecoderError:
            CaptureAbortReason::Reinitialized);
        if(scenario==3)tracker.ObservationLost(ObservationLoss::DecodedQueue,1.6);
        if(scenario==7) {
            const auto middle=tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.7,1,17);
            tracker.AbortCapture(middle,CaptureAbortReason::QueueLoss);
        }
        if(scenario==13)tracker.BeginCapture(DecoderCodec::Flac,0x1000,0,1.7,1,17);
        if(scenario==14) {
            for(unsigned index=0;index<33;++index)
                tracker.BeginCapture(DecoderCodec::Flac,0x2000,0,1.7,1,17);
        }
        const auto next=tracker.BeginCapture(scenario==10?DecoderCodec::Ogg:DecoderCodec::Flac,
            scenario==2?0x2000:0x1000,0,2.0,scenario==11?2:1,20);
        tracker.CompleteCapture(next,3.0,30);
        assert(!tracker.BindCaptureExact(next,"same",scenario==8?"spotify:track:other":"spotify:track:one",
            scenario==9?5:4,3.1,true));
        if(scenario==1) {
            const char* rejection=nullptr;
            assert(!tracker.BindCaptureExact(next,"same","spotify:track:one",4,3.1,true,&rejection));
            assert(rejection&&std::string(rejection)=="restart-predecessor-not-eligible");
        }
    }
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
    TestPreloadedSuccessorAllowsConcurrentOutgoingOutput();
    TestMixedVoiceAndMusicSelectByPlaybackKind();
    TestUnavailableInstanceAndPauseResume();
    TestSuccessorsAndSkips();
    TestCaptureConflictsAndReuse();
    TestDelayedDecodedEvidenceUsesCompletedLifetimeWindow();
    TestProducerSequenceOwnsCompletionBoundaryAndAddressReuse();
    TestCompletedConflictDoesNotPoisonLaterCapture();
    TestProducerSequenceSeparatesStillActiveReusedDecoder();
    TestInvalidatedLatestLifetimeDoesNotFallBackToOlderReader();
    TestDecodedRangeAcrossReusedDecoderRemainsAmbiguous();
    TestEqualProducerBeginsRemainAmbiguous();
    TestAbortLossEpochAndTime();
    TestElapsedTimeNeverRejectsOwnershipEvidence();
    TestBlankIdOutgoingAndSuccessorUseLifecycleStages();
    TestReadyClockSkewCannotRejectLifecycle();
    TestCurrentItemChangeReconsidersOnlyUniqueAmbiguousSuccessor();
    TestExactDecoderIdentityResolvesSameCodecConflict();
    TestExactCompletedSourceDoesNotRequireDecodedOutput();
    TestExactCompleteFlacCanReplaceCleanRestartChain();
    TestExactRestartTransferRejectsUnsafePredecessors();
    TestBoundedCaptureAndOwnerStorage();
    TestBoundedPlaybackStorage();
}
