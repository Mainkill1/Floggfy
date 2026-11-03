#include "../native/decoder_ownership.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace ownership;

static DecodedActivity Output(std::uintptr_t owner,double first,double last,
                              unsigned epoch,std::uint64_t sequence) {
    DecodedActivity out{};out.owner=owner;out.calls=1;out.produced_floats=8192;
    out.consumed_bytes=4096;out.kept_floats=8192;out.first=first;out.last=last;
    out.epoch=epoch;out.sequence=sequence;return out;
}

int main(int argc,char** argv) {
    const unsigned iterations=argc>1?unsigned(std::stoul(argv[1])):20000;
    const std::uint64_t seed=argc>2?std::stoull(argv[2]):4043931417ULL;
    std::mt19937_64 random(seed);
    for(unsigned run=0;run<iterations;++run) {
        const unsigned epoch=1+unsigned(random()%1000000);
        const double base=100+double(random()%100000)/1000;
        const auto owner=std::uintptr_t(0x1000+(random()%4096)*16);
        const auto generation=std::uint64_t(1+random()%1000000);
        const std::string id="play-"+std::to_string(run);
        DecoderOwnershipTracker tracker;tracker.Reset(epoch);
        switch(run%5) {
        case 0: {
            const auto capture=tracker.BeginCapture(DecoderCodec::Flac,owner,0,base,epoch,10);
            tracker.CompleteCapture(capture,base+.2,20);
            // The compressed callback completes before the outer decoded hook
            // is consumed. Producer sequence, not its slightly later clock,
            // keeps the observation in the correct lifetime.
            tracker.ObserveDecoded(Output(owner,base+.21,base+.22,epoch,15));
            assert(tracker.BindCapture(capture,id,"spotify:track:one",generation,base+.3));
            const auto binding=tracker.Binding(capture);
            assert(binding.state==OwnershipState::Bound&&binding.listen_generation==generation);
            break;
        }
        case 1: {
            // One decoder address is reused twice before the worker consumes
            // either decoded observation. Sequence boundaries must not merge.
            const auto first=tracker.BeginCapture(DecoderCodec::Ogg,owner,1,base,epoch,10);
            tracker.CompleteCapture(first,base+.2,20);
            const auto second=tracker.BeginCapture(DecoderCodec::Ogg,owner,2,base+.3,epoch,30);
            tracker.CompleteCapture(second,base+.5,40);
            tracker.ObserveDecoded(Output(owner,base+.1,base+.1,epoch,15));
            tracker.ObserveDecoded(Output(owner,base+.4,base+.4,epoch,35));
            assert(tracker.BindCapture(first,id+"-a","spotify:track:one",generation,base+.6));
            assert(tracker.BindCapture(second,id+"-b","spotify:track:two",generation+1,base+.7));
            assert(tracker.Binding(first).playback_token!=tracker.Binding(second).playback_token);
            break;
        }
        case 2: {
            // A poisoned lifetime is evidence about itself only. A later,
            // nonoverlapping capture must establish ownership independently.
            const auto bad=tracker.BeginCapture(DecoderCodec::Flac,owner,0,base,epoch,10);
            tracker.ObserveDecoded(Output(owner,base-.1,base,epoch,11));
            assert(tracker.Binding(bad).state==OwnershipState::Conflict);
            tracker.CompleteCapture(bad,base+.2,20);
            const auto fresh_owner=owner+0x10000;
            const auto fresh=tracker.BeginCapture(DecoderCodec::Flac,fresh_owner,0,base+100,epoch,30);
            tracker.ObserveDecoded(Output(fresh_owner,base+100.1,base+100.2,epoch,31));
            assert(tracker.BindCapture(fresh,id,"spotify:track:fresh",generation,base+100.3));
            assert(tracker.Binding(fresh).state==OwnershipState::Bound);
            break;
        }
        case 3: {
            // Spotify may expose the playback stage before Ready supplies the
            // listen generation. The exact same token may be upgraded once.
            const auto capture=tracker.BeginCapture(DecoderCodec::Flac,owner,0,base,epoch,10);
            tracker.ObserveTransition(id,"spotify:track:one",base+.05,false,false);
            tracker.ObserveDecoded(Output(owner,base+.1,base+.2,epoch,11));
            tracker.ObserveTransition(id,"spotify:track:one",base+.25,false,false);
            tracker.ObserveDecoded(Output(owner,base+.3,base+.35,epoch,12));
            const auto before=tracker.Binding(capture);
            assert(before.state==OwnershipState::Bound&&!before.listen_generation);
            assert(tracker.BindCapture(capture,id,"spotify:track:one",generation,base+.3));
            assert(tracker.Binding(capture).listen_generation==generation);
            assert(!tracker.BindCapture(capture,id+"-wrong","spotify:track:one",generation+1,base+.4));
            break;
        }
        default: {
            // Lost queue evidence fails closed, but does not contaminate a
            // later tracker epoch/lifetime.
            const auto capture=tracker.BeginCapture(DecoderCodec::Aac,owner,0,base,epoch,10);
            tracker.ObserveDecoded(Output(owner,base+.1,base+.2,epoch,11));
            tracker.ObservationLost(ObservationLoss::DecodedQueue,base+.3);
            assert(tracker.Binding(capture).state!=OwnershipState::Bound);
            tracker.Reset(epoch+1);
            const auto fresh=tracker.BeginCapture(DecoderCodec::Aac,owner,0,base+1,epoch+1,20);
            tracker.ObserveDecoded(Output(owner,base+1.1,base+1.2,epoch+1,21));
            assert(tracker.BindCapture(fresh,id,"spotify:track:fresh",generation,base+1.3));
            break;
        }
        }

        // Random state-machine traffic complements the five directed traces.
        // The oracle intentionally checks only hard invariants: ambiguity and
        // loss fail closed, bindings have unique playback ownership, and a
        // terminal capture can never be resurrected by later observations.
        DecoderOwnershipTracker chaos;chaos.Reset(epoch);
        std::vector<std::uint64_t> tokens;
        std::vector<bool> terminal;
        std::vector<std::uintptr_t> owners;
        unsigned active_epoch=epoch;
        for(unsigned event=0;event<64;++event) {
            const auto choice=unsigned(random()%9);
            if(choice==0||tokens.empty()) {
                const auto candidate=std::uintptr_t(0x1000+(random()%32)*16);
                const auto token=chaos.BeginCapture(DecoderCodec(random()%4),
                    random()%13?candidate:0,unsigned(random()%4),base+event*.01,
                    random()%11?active_epoch:active_epoch+1,1+event*3);
                tokens.push_back(token);terminal.push_back(false);owners.push_back(candidate);
            } else {
                const auto index=std::size_t(random()%tokens.size());
                switch(choice) {
                case 1:
                    chaos.CompleteCapture(tokens[index],
                        random()%7?base+event*.01+.005:base-.1,2+event*3);break;
                case 2:
                    chaos.AbortCapture(tokens[index],CaptureAbortReason(random()%8));break;
                case 3: {
                    auto activity=Output(random()%5?owners[index]:owners[index]+16,
                        base+event*.01,base+event*.01+.004,active_epoch,1+random()%(3+event*3));
                    if(random()%13==0)activity.last=activity.first-.1;
                    if(random()%17==0)activity.epoch=active_epoch+1;
                    if(random()%19==0)activity.calls=0;
                    chaos.ObserveDecoded(activity);break;
                }
                case 4:
                    chaos.ObservePlayback("stage-"+std::to_string(random()%12),
                        random()%5?"spotify:track:one":"spotify:episode:one",
                        random()%4?1+random()%1000:0,base+event*.01);break;
                case 5:
                    chaos.ObserveTransition("stage-"+std::to_string(random()%12),
                        "spotify:track:one",base+event*.01,random()%2,random()%2);break;
                case 6: {
                    const auto was_terminal=terminal[index];
                    const auto ok=chaos.BindCapture(tokens[index],
                        "stage-"+std::to_string(random()%12),"spotify:track:one",
                        1+random()%1000,base+event*.01);
                    if(was_terminal)assert(!ok);
                    if(ok)assert(chaos.Binding(tokens[index]).state==OwnershipState::Bound);
                    break;
                }
                case 7:
                    chaos.ObservationLost(ObservationLoss(random()%4),base+event*.01);break;
                default:
                    ++active_epoch;chaos.Reset(active_epoch);
                    tokens.clear();terminal.clear();owners.clear();break;
                }
            }
            std::vector<std::uint64_t> bound_playbacks;
            for(std::size_t i=0;i<tokens.size();++i) {
                const auto binding=chaos.Binding(tokens[i]);
                if(binding.reason==OwnershipReason::MissingCapture)continue;
                if(terminal[i])assert(binding.state!=OwnershipState::Bound);
                if(binding.state==OwnershipState::Conflict||binding.state==OwnershipState::Invalidated)
                    terminal[i]=true;
                if(binding.state==OwnershipState::Bound) {
                    assert(binding.owner_token&&binding.playback_token);
                    for(const auto playback:bound_playbacks)assert(playback!=binding.playback_token);
                    bound_playbacks.push_back(binding.playback_token);
                }
            }
        }
    }
    std::printf("PASS: seed=%llu ownership lifecycles=%u; directed regressions plus randomized event order, loss, reuse and invalid boundaries\n",
                static_cast<unsigned long long>(seed),iterations);
}
