#include "../native/spotify_hook_discovery.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace {
template<class T> void Put(std::vector<std::uint8_t>& image,std::size_t at,T value) {
    assert(at+sizeof(value)<=image.size());std::memcpy(image.data()+at,&value,sizeof(value));
}
void Bytes(std::vector<std::uint8_t>& image,std::size_t at,std::initializer_list<std::uint8_t> bytes) {
    assert(at+bytes.size()<=image.size());std::copy(bytes.begin(),bytes.end(),image.begin()+at);
}
void Call(std::vector<std::uint8_t>& image,std::uint32_t at,std::uint32_t target) {
    image[at]=0xe8;Put<std::int32_t>(image,at+1,std::int32_t(target)-std::int32_t(at+5));
}
void Literal(std::vector<std::uint8_t>& image,std::uint32_t at,std::uint32_t target,const char* text) {
    Bytes(image,at,{0x48,0x8d,0x15});Put<std::int32_t>(image,at+3,std::int32_t(target)-std::int32_t(at+7));
    std::memcpy(image.data()+target,text,std::strlen(text)+1);
}
std::vector<std::uint8_t> Fixture() {
    std::vector<std::uint8_t> image(0x6000);
    Put<std::uint16_t>(image,0,0x5a4d);Put<std::uint32_t>(image,0x3c,0x80);
    Put<std::uint32_t>(image,0x80,0x4550);Put<std::uint16_t>(image,0x84,0x8664);
    Put<std::uint16_t>(image,0x86,3);Put<std::uint16_t>(image,0x94,240);
    Put<std::uint16_t>(image,0x98,0x20b);Put<std::uint32_t>(image,0x98+56,0x6000);
    Put<std::uint32_t>(image,0x98+108,16);Put<std::uint32_t>(image,0x98+136,0x5000);
    Put<std::uint32_t>(image,0x98+140,7*12);
    for(unsigned i=0;i<3;++i) {
        const auto at=0x188+i*40;Put<std::uint32_t>(image,at+8,i?0x1000:0x3000);
        Put<std::uint32_t>(image,at+12,i?0x3000+i*0x1000:0x1000);
        Put<std::uint32_t>(image,at+36,i?0x40000040:0x60000020);
    }
    // The native inflater constructor at 0x2300 is a leaf with no unwind row.
    const std::uint32_t functions[][2]={{0x1000,0x1800},{0x2000,0x202e},{0x2100,0x2200},
        {0x2400,0x2480},{0x2500,0x2540},{0x2600,0x2640},{0x3000,0x3800}};
    for(unsigned i=0;i<7;++i) {Put(image,0x5000+i*12,functions[i][0]);Put(image,0x5004+i*12,functions[i][1]);}
    // Input is saved in RSI; output is separate. The body is a native string*.
    Bytes(image,0x1020,{0x48,0x8b,0xf2,0x48,0x8b,0xf9});
    Bytes(image,0x1040,{0x48,0x8b,0xca});Call(image,0x1043,0x2600);
    Bytes(image,0x1048,{0x48,0x8b,0x56,0x20});
    Bytes(image,0x1080,{0x48,0x8d,0x4d,0xd0});Call(image,0x1084,0x2000);Bytes(image,0x1089,{0x84,0xc0});
    // Inflate into a local string; finalize before consuming complete plaintext.
    Bytes(image,0x1100,{0x48,0x8d,0x54,0x24,0x38,0x48,0x8d,0x4c,0x24,0x70});Call(image,0x110a,0x2300);
    Bytes(image,0x110f,{0x48,0x8d,0x55,0xa8,0x48,0x39,0x5d,0xc0,0x48,0x0f,0x47,0x55,0xa8,
        0x44,0x8b,0x45,0xb8,0x48,0x8d,0x4c,0x24,0x70});Call(image,0x1125,0x2400);
    Bytes(image,0x112a,{0x48,0x8d,0x4c,0x24,0x70});Call(image,0x112f,0x2500);
    Bytes(image,0x1134,{0x48,0x8d,0x55,0x88,0x48,0x8d,0x4c,0x24,0x48});Call(image,0x113d,0x2000);
    Bytes(image,0x1142,{0x84,0xc0});
    Bytes(image,0x1300,{0x48,0x8d,0x4c,0x24,0x48});Call(image,0x1305,0x2000);Bytes(image,0x130a,{0x84,0xc0});
    Literal(image,0x1400,0x4000,"Received player command: %s");
    Literal(image,0x1500,0x4100,"sent_by_device_id");
    Literal(image,0x1600,0x4200,"Unable to parse connect player command:\n%s");
    Bytes(image,0x2000,{0x48,0x83,0xec,0x38,0x48,0x83,0x7a,0x18,0x0f,0x48,0x8b,0xc2,0x76,0x03,
        0x48,0x8b,0x02,0x48,0x89,0x44,0x24,0x20,0x48,0x8b,0x42,0x10,0x48,0x8d,0x54,0x24,0x20,
        0x48,0x89,0x44,0x24,0x28});Call(image,0x2024,0x2100);
    Bytes(image,0x2029,{0x48,0x83,0xc4,0x38,0xc3});
    Bytes(image,0x2300,{0x48,0x8d,0x05});Put<std::int32_t>(image,0x2303,0x4300-0x2307);
    Bytes(image,0x2307,{0x48,0x89,0x51,0x08,0x48,0x89,0x01,0x48,0x8b,0xc1,
        0x48,0xc7,0x41,0x10,0x00,0x00,0x00,0x00,0xc3});return image;
}
void Reject(std::vector<std::uint8_t> image,spotify::DiscoveryResult expected) {
    spotify::ConnectDecodeTargets out{1,2,3};
    assert(spotify::DiscoverConnectDecodeTargets(image.data(),image.size(),out)==expected);
    assert(!out.parser&&!out.plain_return&&!out.decoded_return);
}
}
void TestConnectHookDiscovery(const std::uint8_t* mapped,std::size_t size) {
    spotify::ConnectDecodeTargets out{};
    assert(spotify::DiscoverConnectDecodeTargets(mapped,size,out)==spotify::DiscoveryResult::Found);
    assert(out.parser&&out.plain_return&&out.decoded_return&&out.plain_return!=out.decoded_return);
    assert(out.parser<size&&out.plain_return<size&&out.decoded_return<size);
}
void TestConnectHookDiscovery() {
    auto image=Fixture();spotify::ConnectDecodeTargets out{};
    TestConnectHookDiscovery(image.data(),image.size());
    assert(spotify::DiscoverConnectDecodeTargets(image.data(),image.size(),out)==spotify::DiscoveryResult::Found);
    assert(out.parser==0x2000&&out.plain_return==0x130a&&out.decoded_return==0x1142);
    Reject({},spotify::DiscoveryResult::InvalidImage);
    auto bad=image;bad[0]=0;Reject(bad,spotify::DiscoveryResult::InvalidImage);
    bad=image;bad[0x4000]='X';Reject(bad,spotify::DiscoveryResult::MissingTarget);
    bad=image;bad[0x2008]=0x17;Reject(bad,spotify::DiscoveryResult::InconsistentLayout); // changed SSO threshold
    bad=image;bad[0x2019]=0x08;Reject(bad,spotify::DiscoveryResult::InconsistentLayout); // changed size field
    bad=image;Call(bad,0x1305,0x2100);Reject(bad,spotify::DiscoveryResult::InconsistentLayout);
    bad=image;Call(bad,0x112f,0x4500);Reject(bad,spotify::DiscoveryResult::InconsistentLayout); // non-executable finalizer
    bad=image;Call(bad,0x2024,0x4500);Reject(bad,spotify::DiscoveryResult::InconsistentLayout);
    bad=image;bad[0x4100]='X';Reject(bad,spotify::DiscoveryResult::InconsistentLayout);
    bad=image;Literal(bad,0x3100,0x4000,"Received player command: %s");Reject(bad,spotify::DiscoveryResult::AmbiguousTarget);
    bad=image;Literal(bad,0x1450,0x4000,"Received player command: %s");Reject(bad,spotify::DiscoveryResult::AmbiguousTarget);
    bad=image;Bytes(bad,0x1350,{0x48,0x8d,0x4c,0x24,0x48});Call(bad,0x1355,0x2000);
    Bytes(bad,0x135a,{0x84,0xc0});Reject(bad,spotify::DiscoveryResult::InconsistentLayout);
    bad=image;Put<std::uint32_t>(bad,0x5010,0x2028);Reject(bad,spotify::DiscoveryResult::InconsistentLayout);
    bad=image;bad[0x2319]=0x90;Reject(bad,spotify::DiscoveryResult::InconsistentLayout);
}
