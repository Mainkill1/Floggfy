#include "../native/aac_stream.h"
#include "spac_fixture.h"
#include <cstdio>
#include <cstdlib>
using namespace history;
static void check(bool value,const char* message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static AacParse push(SpacStream& stream,const std::vector<uint8_t>& bytes){return stream.Push(bytes.data(),bytes.size());}
static void rejected(const std::vector<uint8_t>& bytes,const char* message) {
    AacInfo info;info.rate=123;std::vector<std::vector<uint8_t>> packets{{9}};std::string error;
    check(!ReadSpac(bytes,info,packets,error)&&!error.empty()&&info.rate==123&&packets==std::vector<std::vector<uint8_t>>{{9}},message);
}
int RunSpacStreamTests() {
    check(AacExtractionFitsBudget(1024,1024,10),"small extraction fits peak budget");
    check(!AacExtractionFitsBudget(size_t(-1),1024,10),"oversized capacity rejected without overflow or allocation");
    check(!AacExtractionFitsBudget(1024,2048,10),"inconsistent source size rejected");
    check(!AacExtractionFitsBudget(1024,1024,(size_t{1}<<20)+1),"packet object overhead bounded before allocation");
    check(!AacExtractionFitsBudget(300u*1024u*1024u,300u*1024u*1024u,10),"source plus extracted payload must fit peak budget");
    for(unsigned object:{2u,5u,29u})for(bool shared:{false,true}) {
        const auto bytes=spac_fixture::stream(object,shared);
        SpacStream fragmented;
        for(size_t i=0;i<bytes.size();++i)
            check(fragmented.Push(&bytes[i],1)==(i+1==bytes.size()?AacParse::Complete:AacParse::NeedMore),"every byte boundary remains incremental");
        check(fragmented.Complete()&&fragmented.info.rate==44100&&fragmented.info.priming==100&&fragmented.info.total_samples==2500,"timing and priming preserved");
        check(fragmented.Duration()==2500.0/44100,"duration excludes priming");
        check(fragmented.Push(nullptr,0)==AacParse::Complete,"empty call after completion harmless");
        const uint8_t extra=0;check(fragmented.Push(&extra,1)==AacParse::Invalid&&!fragmented.Complete(),"extra bytes after completion invalidate");
        AacInfo info;std::vector<std::vector<uint8_t>> packets;std::string error;
        check(ReadSpac(bytes,info,packets,error)&&error.empty(),"complete extraction");
        check(info.config==spac_fixture::asc(object,object==29?1:2),"exact decoder config preserved");
        check(packets.size()==(object==2?3u:2u),"rendered samples determine access unit count");
        for(const auto& packet:packets)check(packet==std::vector<uint8_t>({0x21,0x10,0x04}),"original access units including shared byte restored");
        for(size_t cut=0;cut<bytes.size();++cut)rejected({bytes.begin(),bytes.begin()+cut},"every truncated prefix rejected by complete extraction");
    }
    // Observed native HE-AACv2 setup, paired with synthetic access units only.
    check(spac_fixture::asc(29,1)==std::vector<uint8_t>({0xeb,0x8a,0x08,0x00}),"observed HE-AACv2 config encoding");
    auto observed=spac_fixture::page(true,0,{spac_fixture::config(29,8314226,5119)});
    const uint32_t expected=(8314226u+5119u+2047u)/2048u;
    for(uint32_t base=0;base<expected;) {
        const uint32_t count=expected-base>11?11:expected-base;
        spac_fixture::append(observed,spac_fixture::page(false,base,std::vector<std::vector<uint8_t>>(count,{0x21,0x10}),true));
        base+=count;
    }
    AacInfo observed_info;std::vector<std::vector<uint8_t>> observed_packets;std::string observed_error;
    check(ReadSpac(observed,observed_info,observed_packets,observed_error)&&observed_packets.size()==expected&&observed_info.priming==5119,"observed rendered timing and final trim boundary");
    // Cross the compact frame-index width boundary with bounded tiny packets.
    auto indexed=spac_fixture::page(true,0,{spac_fixture::config(2,32770u*1024u,0)});
    for(uint32_t base=0;base<32770;) {
        const uint32_t count=32770-base>250?250:32770-base;
        spac_fixture::append(indexed,spac_fixture::page(false,base,std::vector<std::vector<uint8_t>>(count,{0x21})));
        base+=count;
    }
    SpacStream indexed_stream;check(push(indexed_stream,indexed)==AacParse::Complete,"two and three byte frame indices remain contiguous");
    auto valid=spac_fixture::stream();
    auto reserved=spac_fixture::page(true,0,{spac_fixture::config()});reserved[4]|=2;
    SpacStream reserved_stream;check(push(reserved_stream,reserved)==AacParse::Unsupported,"unproven page flags unsupported");
    auto malformed=spac_fixture::page(false,0,{{1}});malformed[8]=255;
    rejected(malformed,"unterminated packet lace");
    rejected(spac_fixture::page(true,0,{spac_fixture::config(),spac_fixture::config()}),"duplicate stream configuration");
    auto corrupt=valid;corrupt.back()^=1;rejected(corrupt,"checksum mismatch");
    corrupt=valid;corrupt[0]='O';rejected(corrupt,"different magic");
    rejected(spac_fixture::page(false,0,{{1,2,3}}),"headerless seek cannot become complete recording");
    auto setup=spac_fixture::page(true,0,{spac_fixture::config()});
    auto gap=setup;spac_fixture::append(gap,spac_fixture::page(false,1,{{1},{2},{3}}));rejected(gap,"initial frame gap");
    auto duplicate=setup;spac_fixture::append(duplicate,spac_fixture::page(false,0,{{1}}));spac_fixture::append(duplicate,spac_fixture::page(false,0,{{2},{3}}));rejected(duplicate,"duplicate frame base");
    auto excess=setup;spac_fixture::append(excess,spac_fixture::page(false,0,{{1},{2},{3},{4}}));rejected(excess,"extra access units after expected end");
    auto restart=setup;spac_fixture::append(restart,setup);spac_fixture::append(restart,spac_fixture::page(false,0,{{1},{2},{3}}));rejected(restart,"repeated setup restart");
    auto bad=spac_fixture::config();bad[2]='o';rejected(spac_fixture::page(true,0,{bad}),"non-mp4a codec");
    bad=spac_fixture::config(42);SpacStream unsupported;
    check(push(unsupported,spac_fixture::page(true,0,{bad}))==AacParse::Unsupported,"unproven xHE unsupported");
    check(push(unsupported,valid)==AacParse::Unsupported,"unsupported result remains sticky");
    bad=spac_fixture::config();bad[1]=1;rejected(spac_fixture::page(true,0,{bad}),"unknown setup version");
    bad=spac_fixture::config();bad[10]=8;rejected(spac_fixture::page(true,0,{bad}),"unsupported channel layout");
    bad=spac_fixture::config();bad[11]=bad[12]=bad[13]=0;rejected(spac_fixture::page(true,0,{bad}),"zero rate");
    bad=spac_fixture::config();bad[16]=bad[17]=0;rejected(spac_fixture::page(true,0,{bad}),"zero samples per access unit");
    bad=spac_fixture::config();bad[18]=65;bad[19]=0;bad.resize(85);rejected(spac_fixture::page(true,0,{bad}),"decoder config cap");
    bad=spac_fixture::config(2,1000000000,0);rejected(spac_fixture::page(true,0,{bad}),"six hour duration bound");
    bad=spac_fixture::config(2,2000000,0);bad[16]=1;bad[17]=0;rejected(spac_fixture::page(true,0,{bad}),"one million frame bound");
    SpacStream null_input;check(null_input.Push(nullptr,1)==AacParse::Invalid,"null nonempty input rejected");
    // Maximum valid lacing exercise: one packet spans several 255 laces.
    auto large=spac_fixture::page(true,0,{spac_fixture::config(2,1,0)});
    const std::vector<uint8_t> access_unit(60000,0x21);
    spac_fixture::append(large,spac_fixture::page(false,0,{access_unit}));
    AacInfo info;std::vector<std::vector<uint8_t>> packets;std::string error;
    check(ReadSpac(large,info,packets,error)&&packets==std::vector<std::vector<uint8_t>>{access_unit},"bounded large packet and multi-lace extraction");
    std::puts("PASS: bounded incremental SpAC validates checksums/config/timing and preserves AAC access units; rejects gaps, restarts, truncation and unproven codecs");
    return 0;
}
#ifndef FLOGGFY_COMBINED_TEST
int main(){return RunSpacStreamTests();}
#endif
