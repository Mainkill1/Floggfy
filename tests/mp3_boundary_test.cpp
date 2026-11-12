#include "../native/mp3_stream.h"
#include "../native/compressed_buffer.h"
#include <cassert>
#include <algorithm>
#include <cstdio>
using namespace history;
static std::vector<uint8_t> Source(uint8_t payload) {
    std::vector<uint8_t> bytes={'I','D','3',4,0,0,0,0,0,0};
    for(unsigned i=0;i<3;++i){const auto at=bytes.size();bytes.resize(at+417,payload);bytes[at]=0xff;bytes[at+1]=0xfb;bytes[at+2]=0x90;bytes[at+3]=0;}
    return bytes;
}
int main() {
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
