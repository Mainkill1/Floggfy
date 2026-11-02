#include "../native/ogg_history_core.h"
#include "../native/recording_file.h"
#include "../native/aac_stream.h"
#include "../native/flac_history_core.h"
#include "../native/client_playback.h"
#include "../native/spotify_hook_discovery.h"
#include "../native/decoded_output_speed.h"
#include "recording_fixture.h"
#include "spac_fixture.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include <thread>
#include <limits>
using namespace history;
static void Exercise(const std::vector<uint8_t>& bytes,std::mt19937_64& rng) {
    Page page;ParsePage(bytes.data(),bytes.size(),page);
    uint32_t serial=0,sequence=0;ParsePageIdentity(bytes.data(),bytes.size(),serial,sequence);
    Stream ogg;ogg.Push(bytes.data(),bytes.size());
    RecordingFile recording;InspectRecording(bytes.data(),bytes.size(),recording);
    FlacInfo info;size_t audio=0;ParseFlacMetadata(bytes,info,audio);
    SpacStream whole,fragmented;const auto expected=whole.Push(bytes.data(),bytes.size());
    size_t at=0;AacParse actual=AacParse::NeedMore;
    while(at<bytes.size()) {
        const auto count=std::min(bytes.size()-at,size_t(1+rng()%127));
        actual=fragmented.Push(bytes.data()+at,count);at+=count;
    }
    if(!bytes.empty())assert(actual==expected);
    assert(whole.Complete()==fragmented.Complete());
    ClientPlaybackRecord playback;ClientTransition transition;std::string error;
    const std::string text(bytes.begin(),bytes.end());
    ParseClientPlayback(text,playback,error);ParseClientTransition(text,transition);
    spotify::DecodedOutputTarget target;spotify::HookTargets targets;
    spotify::DiscoverDecodedOutputTarget(bytes.data(),bytes.size(),target);
    spotify::DiscoverHookTargets(bytes.data(),bytes.size(),targets);
}
int main(int argc,char** argv) {
    const unsigned iterations=argc>1?unsigned(std::stoul(argv[1])):20000;
    const uint64_t seed=argc>2?std::stoull(argv[2]):0xf1099f19;
    std::mt19937_64 rng(seed);
    std::vector<std::vector<uint8_t>> corpus={recording_fixture::fixture(),recording_fixture::ogg(),spac_fixture::stream()};
    const std::string protocol="v=1&title=Song&artist=Artist&album=Album&duration=100&uri=spotify%3Atrack%3A0000000000000000000001&position=0&sampled_ms=1793300000000&paused=0&buffering=0&local=1&speed=10";
    corpus.emplace_back(protocol.begin(),protocol.end());
    AacInfo aac;std::vector<std::vector<uint8_t>> packets;std::string error;
    assert(ReadSpac(corpus[2],aac,packets,error));std::vector<uint8_t> m4a;
    assert(MakeAacM4a(aac,packets,{},m4a,error));corpus.push_back(m4a);
    for(const auto& base:corpus) {
        Exercise(base,rng);
        for(size_t cut=0;cut<base.size();++cut)Exercise({base.begin(),base.begin()+cut},rng);
    }
    for(unsigned run=0;run<iterations;++run) {
        auto bytes=corpus[rng()%corpus.size()];
        switch(rng()%5) {
        case 0:bytes.resize(rng()%4096);for(auto& byte:bytes)byte=uint8_t(rng());break;
        case 1:if(!bytes.empty())bytes.resize(rng()%bytes.size());break;
        case 2:bytes.insert(bytes.begin()+rng()%(bytes.size()+1),size_t(rng()%64),uint8_t(rng()));break;
        default:for(unsigned edit=0;edit<1+rng()%16&&!bytes.empty();++edit)bytes[rng()%bytes.size()]^=uint8_t(1u<<(rng()%8));
        }
        Exercise(bytes,rng);
        // Repair Ogg CRC after mutations to reach sequence/granule/header guards.
        if(bytes.size()>=27&&bytes[0]=='O'&&bytes[1]=='g'&&bytes[2]=='g'&&bytes[3]=='S') {
            for(unsigned i=22;i<26;++i)bytes[i]=0;
            uint32_t crc=0;for(auto b:bytes){crc^=uint32_t(b)<<24;for(unsigned bit=0;bit<8;++bit)crc=(crc<<1)^((crc&0x80000000)?0x04c11db7:0);}
            for(unsigned i=0;i<4;++i)bytes[22+i]=uint8_t(crc>>(8*i));Exercise(bytes,rng);
        }
        Listen listen;double time=100;
        for(unsigned event=0;event<32;++event) {
            time+=double(rng()%4000)/1000;
            const double position=(rng()%20==0)?std::numeric_limits<double>::quiet_NaN():double(rng()%11000)/100;
            const unsigned rates[]={1,2,4,8,10,0,3,~0u};
            listen.Observe(rng()%4?"one":"two",position,100,rng()%2,time,rates[rng()%8]);
            assert(listen.completed_generation<=listen.generation);
        }
    }
    decoded_output::Accelerator accelerator;std::vector<std::thread> threads;
    for(unsigned lane=0;lane<8;++lane)threads.emplace_back([&,lane]{
        for(unsigned i=0;i<100000;++i) {
            const unsigned rates[]={1,2,4,8,10};const auto produced=size_t(2+2*(i%8192));
            const auto kept=accelerator.Keep(reinterpret_cast<void*>(uintptr_t(0x1000+(i%256)*16)),produced,rates[(i+lane)%5],i%3);
            assert(kept>0&&kept<=produced&&kept%2==0);
        }
    });
    for(auto& thread:threads)thread.join();
    std::printf("PASS: seed=%llu mutations=%u; all fixture truncations; fragmented SpAC equivalence; randomized listens; 800000 concurrent frame reductions\n",(unsigned long long)seed,iterations);
}
