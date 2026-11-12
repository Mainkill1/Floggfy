#include "../native/mp3_stream.h"
#include "../native/compressed_buffer.h"
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <cstring>
using namespace history;
static std::vector<uint8_t> Source(uint8_t payload) {
    std::vector<uint8_t> bytes={'I','D','3',4,0,0,0,0,0,0};
    for(unsigned i=0;i<3;++i){const auto at=bytes.size();bytes.resize(at+417,payload);bytes[at]=0xff;bytes[at+1]=0xfb;bytes[at+2]=0x90;bytes[at+3]=0;}
    return bytes;
}
int main() {
    {
        auto declared=Source(0);std::memcpy(declared.data()+46,"Xing",4);
        declared[53]=3;declared[57]=2;declared[60]=4;declared[61]=0xe3; // 3 physical frames, 1251 bytes.
        CompressedBuffer growing;Mp3Info scan;size_t boundary=0;
        for(size_t i=0;i<declared.size();++i) {
            assert(growing.Append(&declared[i],1,65536));assert(!FindMp3StreamStart(growing,scan,boundary));
            if(i==426||i==843) {
                assert(scan.frames==(i==426?1u:2u)&&!scan.complete&&!scan.DeclaredComplete()&&
                       scan.declared_frames==3&&scan.declared_bytes==1251);
                Mp3Info prefix;assert(InspectMp3Prefix(growing,0,prefix)&&!prefix.complete&&!prefix.DeclaredComplete());
            }
        }
        assert(scan.DeclaredComplete()&&scan.frames==3&&scan.audio_size==1251);
        auto next=Source(0);assert(growing.Append(next.data(),2,65536));
        assert(FindMp3StreamStart(growing,scan,boundary)&&boundary==1261&&scan.DeclaredComplete()&&
               "source boundary retains the predecessor's validated declaration");
        Mp3Info exact;assert(InspectMp3Range(growing,0,boundary,exact)&&exact.DeclaredComplete());

        std::vector<uint8_t> tagged=declared;tagged.resize(1389,0);
        tagged[1261]='T';tagged[1262]='A';tagged[1263]='G';
        tagged.insert(tagged.end(),next.begin(),next.end());CompressedBuffer trailer;scan={};
        assert(trailer.Append(tagged.data(),tagged.size(),65536));
        assert(FindMp3StreamStart(trailer,scan,boundary)&&boundary==1389&&scan.DeclaredComplete()&&
               scan.audio_size==1251&&"ID3v1 trailer excluded from declared audio length at source boundary");
        assert(InspectMp3Range(trailer,0,boundary,exact)&&exact.DeclaredComplete());

        declared.resize(844);CompressedBuffer short_source;
        assert(short_source.Append(declared.data(),declared.size(),65536));scan={};
        assert(!FindMp3StreamStart(short_source,scan,boundary)&&!scan.complete&&scan.declared_frames==3);
        assert(short_source.Append(next.data(),next.size(),65536));
        assert(FindMp3StreamStart(short_source,scan,boundary)&&boundary==844&&
               !scan.complete&&!scan.DeclaredComplete()&&scan.declared_frames==3&&scan.declared_bytes==1251&&
               "following source cannot make a short declared predecessor complete");
        assert(!InspectMp3Range(short_source,0,boundary,exact));
    }
    auto first=Source(0),second=Source(42);
    const uint8_t fake[]={'I','D','3',4,0,0,0,0,0,0};std::copy(std::begin(fake),std::end(fake),first.begin()+80);
    CompressedBuffer bytes;Mp3Info cursor;size_t cut=0;
    for(size_t i=0;i<first.size();++i){assert(bytes.Append(&first[i],1,65536));assert(!FindMp3StreamStart(bytes,cursor,cut));}
    assert(cursor.frames==3&&cursor.samples==3456&&"ID3 inside compressed frame payload is not a source boundary");
    assert(bytes.Append(second.data(),2,65536));assert(FindMp3StreamStart(bytes,cursor,cut)&&cut==1261);
    Mp3Info next;assert(!InspectMp3Prefix(bytes,cut,next));
    assert(bytes.Append(second.data()+2,8,65536)&&!InspectMp3Prefix(bytes,cut,next));
    assert(bytes.Append(second.data()+10,417,65536)&&InspectMp3Prefix(bytes,cut,next)&&next.frames==1);
    assert(bytes.Append(second.data()+427,second.size()-427,65536));
    CompressedBuffer suffix;assert(bytes.SplitSuffix(cut,suffix,65536));
    assert(bytes.Flatten()==first&&suffix.Flatten()==second);
    auto legacy=second;legacy[3]=2;CompressedBuffer legacy_bytes;
    assert(legacy_bytes.Append(legacy.data(),legacy.size(),65536)&&InspectMp3Prefix(legacy_bytes,0,next)&&next.frames==3);
    CompressedBuffer invalid;auto bad=second;bad[3]=1;
    assert(invalid.Append(bad.data(),bad.size(),65536)&&!InspectMp3Prefix(invalid,0,next));
    CompressedBuffer multiple;auto joined=first;joined.insert(joined.end(),second.begin(),second.end());joined.insert(joined.end(),second.begin(),second.end());
    assert(multiple.Append(joined.data(),joined.size(),65536));cursor={};
    assert(FindMp3StreamStart(multiple,cursor,cut)&&!InspectMp3Prefix(multiple,cut,next)&&"multiple queued source starts cannot masquerade as one unique suffix");
    std::vector<uint8_t> trailer(128,0);trailer[0]='T';trailer[1]='A';trailer[2]='G';
    auto tagged=first;tagged.insert(tagged.end(),trailer.begin(),trailer.end());tagged.insert(tagged.end(),second.begin(),second.end());
    CompressedBuffer id3v1;assert(id3v1.Append(tagged.data(),tagged.size(),65536));cursor={};
    assert(FindMp3StreamStart(id3v1,cursor,cut)&&cut==first.size()+trailer.size()&&InspectMp3Prefix(id3v1,cut,next)&&
           "a valid ID3v1 trailer before the next ID3 header must stay with the predecessor");
    puts("PASS: incremental frame-aligned MP3 boundaries retain prefetched/split headers and reject payload lookalikes or ambiguous suffixes");
}
