#include "../native/existing_quality.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>
using namespace history;
struct Allowance {size_t maximum=100,last=0,calls=0;};
static bool Permit(void* context,size_t peak) {
    auto& allowance=*static_cast<Allowance*>(context);allowance.last=peak;++allowance.calls;return peak<=allowance.maximum;
}
int main() {
    size_t peak=0;const auto maximum=std::numeric_limits<size_t>::max();
    assert(EstimateRecordingReadMemory(100,10,peak)&&peak==8*1024*1024+420);
    assert(!EstimateRecordingReadMemory(maximum,1,peak)&&peak==0);
    assert(!EstimateRecordingReadMemory(10,11,peak));
    assert(EstimatePublicationUpgradeMemory(Codec::Flac,64u<<20,64u<<20,4096,4096,peak));
    assert(peak==(328u<<20)+327680&&peak<(512u<<20));
    assert(!EstimatePublicationUpgradeMemory(Codec::Flac,4,maximum,4,1,peak)&&peak==0);
    assert(!EstimatePublicationUpgradeMemory(Codec::Vorbis,4,maximum/8,4,1,peak));
    assert(EstimateRecordingReadMemory(180u<<20,4096,peak,true));
    assert(peak==(368u<<20)+131072);
    assert(!EstimateRecordingReadMemory(maximum/2+1,0,peak,true)&&peak==0);
    const std::vector<uint8_t> mp4={0,0,0,8,'f','t','y','p',0,0,0,8,'m','o','o','v'};
    RecordingMemoryPlan mp4_plan;
    assert(ProbeRecordingMemory(mp4.data(),mp4.size(),mp4_plan));
    assert(mp4_plan.metadata_bytes==8&&mp4_plan.inspection_peak==8u*1024*1024+288);
    assert(EstimateRecordingReadMemory(180u<<20,4096,peak));
    assert(peak-(180u<<20)==(8u<<20)+131072); // FLAC view inspection has no full audio copy.
    Allowance allowance;
    {
        RecordingReadMemoryScope outer(Permit,&allowance);outer.SetRetainedBytes(10);
        {RecordingReadMemoryScope inner;inner.SetRetainedBytes(20);
         assert(inner.Request(30)&&allowance.last==60);
         assert(!inner.Request(maximum)&&inner.Denied()&&outer.Denied());}
        assert(outer.Request(1)&&allowance.last==11);
    }
    const auto calls=allowance.calls;
    assert(RecordingReadMemoryScope::AdmitCurrent(maximum)&&allowance.calls==calls);
    const std::vector<uint8_t> flac={'f','L','a','C',0x80,0,0,4,0,0,0,0,1,2,3,4};
    RecordingMemoryPlan plan;size_t read_bytes=0;
    assert(ProbeRecordingMemory(flac.size(),[&](size_t at,uint8_t* out,size_t n){
        read_bytes+=n;if(at>flac.size()||n>flac.size()-at)return false;
        std::memcpy(out,flac.data()+at,n);return true;
    },plan));
    assert(plan.file_bytes==16&&plan.metadata_bytes==12&&read_bytes==20);
    assert(ProbeRecordingMemory(flac.data(),flac.size(),plan));
    assert(!ProbeRecordingMemory(flac.data(),9,plan));
    std::vector<uint8_t> mp3={'I','D','3',4,0,0,0,0,0,2,1,2,3,4};
    assert(ProbeRecordingMemory(mp3.data(),mp3.size(),plan)&&plan.metadata_bytes==12);
    mp3[9]=128;assert(!ProbeRecordingMemory(mp3.data(),mp3.size(),plan));
    std::puts("PASS: allocation-free container memory probes and overflow-safe publication peaks");
}
