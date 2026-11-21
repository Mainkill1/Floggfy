#include "../native/media/ogg_history_core.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
using namespace history;
static void check(bool value, const char* name) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}
static void put(std::vector<uint8_t>& v, unsigned at, uint64_t n, unsigned count) {
    for(unsigned i=0;i<count;i++) v[at+i] = uint8_t(n >> (i*8));
}
// Fixture CRC is deliberately independent of the production implementation.
static void checksum(std::vector<uint8_t>& v) {
    put(v,22,0,4); uint32_t crc=0;
    for (auto b : v) { crc ^= uint32_t(b)<<24;
        for(int i=0;i<8;i++) crc = crc&0x80000000 ? (crc<<1)^0x04c11db7 : crc<<1;
    } put(v,22,crc,4);
}
static std::vector<uint8_t> page(uint32_t seq, uint8_t flags, int64_t samples,
                                 bool vorbis = false,uint32_t serial=1234) {
    std::vector<uint8_t> v(58,0); std::memcpy(v.data(),"OggS",4);
    v[5]=flags; put(v,6,uint64_t(samples),8); put(v,14,serial,4); put(v,18,seq,4);
    v[26]=1; v[27]=30;
    if(vorbis) { std::memcpy(v.data()+28,"\x01vorbis",7); v[39]=2; put(v,40,44100,4); }
    checksum(v); return v;
}
static Result push(Stream& s, const std::vector<uint8_t>& v) { return s.Push(v.data(),v.size()); }
int main() {
    auto first=page(1,2,0,true), middle=page(2,0,44100), end=page(3,4,88200);
    Page p; check(ParsePage(first.data(),first.size(),p),"valid Ogg CRC accepted");
    check(p.vorbis_start && p.rate==44100 && p.channels==2,"Vorbis identification parsed");
    auto bad=first; bad.back()^=1;
    check(!ParsePage(bad.data(),bad.size(),p),"bad CRC rejected");
    uint32_t bad_serial=0,bad_sequence=0;
    check(ParsePageIdentity(bad.data(),bad.size(),bad_serial,bad_sequence)&&
          bad_serial==1234&&bad_sequence==1,
          "bounded identity remains available for isolated corruption cleanup");
    check(!ParsePage(first.data(),first.size()-1,p),"truncated page rejected");
    Stream s; check(push(s,page(0,2,0))==Result::Ignore,"custom Spotify page ignored");
    check(push(s,first)==Result::Begin,"first Vorbis sequence may be one");
    check(push(s,middle)==Result::Append,"contiguous page appended");
    check(push(s,end)==Result::Complete && s.pages==3 && s.Duration()==2,"EOS publishes full stream");
    Stream gap; push(gap,first);
    check(push(gap,end)==Result::Invalid && !gap.complete && gap.rejection==OggRejection::Sequence,
          "page gap cannot publish and reports sequence discontinuity");
    Stream partial; check(push(partial,middle)==Result::Ignore && partial.rejection==OggRejection::Inactive,
          "partial listen without BOS ignored with inactive reason");
    Stream corrupt; push(corrupt,first);
    check(push(corrupt,bad)==Result::Invalid && corrupt.rejection==OggRejection::InvalidPage,
          "CRC failure invalidates stream with invalid-page reason");
    Stream backwards; push(backwards,first); push(backwards,middle);
    check(push(backwards,page(3,4,100))==Result::Invalid && backwards.rejection==OggRejection::GranuleRegression,
          "backward granule rejected with precise reason");
    Stream serial;push(serial,first);
    check(push(serial,page(2,0,44100,false,4321))==Result::Invalid && serial.rejection==OggRejection::Serial,
          "different logical stream reports serial change");
    check(push(serial,first)==Result::Begin && serial.rejection==OggRejection::None,
          "new valid BOS clears previous rejection");
    std::puts("PASS: Ogg page integrity and logical streams");
}
