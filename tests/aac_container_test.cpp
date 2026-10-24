#include "../native/aac_stream.h"
#include "artwork_fixture.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using namespace history;
namespace {
using Bytes=std::vector<uint8_t>;
uint32_t Read(const Bytes& b,size_t at){assert(at+4<=b.size());return (uint32_t(b[at])<<24)|(uint32_t(b[at+1])<<16)|(uint32_t(b[at+2])<<8)|b[at+3];}
void Write(Bytes& b,size_t at,uint32_t n){assert(at+4<=b.size());for(unsigned i=0;i<4;++i)b[at+i]=uint8_t(n>>(24-8*i));}
void Append(Bytes& b,uint32_t n){for(unsigned i=0;i<4;++i)b.push_back(uint8_t(n>>(24-8*i)));}
Bytes Atom(const char* kind,const Bytes& payload){Bytes out;Append(out,uint32_t(payload.size()+8));out.insert(out.end(),kind,kind+4);out.insert(out.end(),payload.begin(),payload.end());return out;}
size_t Find(const Bytes& b,size_t start,size_t end,const char* kind){for(size_t p=start;p<end;){const auto n=Read(b,p);assert(n>=8&&n<=end-p);if(!std::memcmp(b.data()+p+4,kind,4))return p;p+=n;}assert(false);return 0;}
size_t Path(const Bytes& b,std::initializer_list<const char*> names){size_t start=0,end=b.size(),found=0;for(const auto* name:names){found=Find(b,start,end,name);start=found+8+(!std::memcmp(name,"meta",4)?4:0);end=found+Read(b,found);}return found;}
Bytes At(const Bytes& b,std::initializer_list<const char*> names){const auto at=Path(b,names);return Bytes(b.begin()+at,b.begin()+at+Read(b,at));}
Bytes Insert(const Bytes& source,std::initializer_list<const char*> path,const Bytes& atom){
    Bytes out=source;std::vector<size_t> ancestors;size_t start=0,end=source.size(),found=0;
    for(const auto* name:path){found=Find(source,start,end,name);ancestors.push_back(found);start=found+8+(!std::memcmp(name,"meta",4)?4:0);end=found+Read(source,found);}
    out.insert(out.begin()+end,atom.begin(),atom.end());for(const auto at:ancestors)Write(out,at,Read(source,at)+uint32_t(atom.size()));return out;
}
std::vector<std::string> Values(const Tags& tags,const std::string& key){std::vector<std::string> result;for(const auto& f:tags.fields)if(f.first==key)result.push_back(f.second);return result;}
AacFile Inspect(const Bytes& b){AacFile f;assert(InspectAacM4a(b.data(),b.size(),f)&&f.complete);return f;}
void SameAudio(const Bytes& a,const Bytes& b){assert(At(a,{"mdat"})==At(b,{"mdat"}));assert(At(a,{"moov","trak","mdia"})==At(b,{"moov","trak","mdia"}));assert(At(a,{"moov","trak","edts"})==At(b,{"moov","trak","edts"}));}
void Rejected(const Bytes& b){AacFile out;out.complete=true;assert(!InspectAacM4a(b.data(),b.size(),out)&&!out.complete);}
}
int RunAacContainerTests(){
    // Actual native SpAC header configuration. Payload sentinels exercise exact
    // remuxing only; this test never claims to decode synthetic AAC access units.
    AacInfo info;info.rate=44100;info.channels=2;info.samples_per_packet=2048;
    info.priming=5119;info.total_samples=8314226;info.config={0xeb,0x8a,0x08,0x00};
    const auto count=(uint64_t(info.total_samples)+info.priming+2047)/2048;
    std::vector<Bytes> packets;Bytes expected;
    for(uint32_t i=0;i<count;++i){packets.push_back({uint8_t(i),uint8_t(i>>8),0xa5,0});expected.insert(expected.end(),packets.back().begin(),packets.back().end());}
    Tags tags;tags.fields={{"TITLE","Song \xE2\x99\xAB"},{"ARTIST","Artist"},{"ALBUM","Album"},{"ALBUMARTIST","Album Artist"},{"DATE","2025-12-31"},{"YEAR","2025"},{"LYRICS","[00:00.00] Words\n"},{"TRACKNUMBER","2"},{"TRACKTOTAL","10"},{"DISCNUMBER","1"},{"DISCTOTAL","2"},{"SPOTIFY_URI","spotify:track:fixture"},{"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_CAPTURE","native compressed AAC input"},{"CUSTOM_REPEAT","one"},{"CUSTOM_REPEAT","two"}};
    tags.cover=artwork_fixture::png();tags.mime="image/png";std::string error;Bytes original;
    assert(MakeAacM4a(info,packets,tags,original,error));const auto inspected=Inspect(original);
    assert(inspected.info.config==info.config&&inspected.info.rate==44100&&inspected.info.channels==2&&inspected.info.samples_per_packet==2048&&inspected.info.priming==5119&&inspected.info.total_samples==8314226);
    const auto media=At(original,{"mdat"});assert(Bytes(media.begin()+8,media.end())==expected&&inspected.audio_bytes==expected.size());
    assert(inspected.has_artwork&&inspected.artwork_edit_safe&&inspected.covers.size()==1&&inspected.tags.cover==tags.cover);
    assert(Values(inspected.tags,"CUSTOM_REPEAT")==std::vector<std::string>({"one","two"}));
    assert(Values(inspected.tags,"TITLE")==Values(tags,"TITLE")&&Values(inspected.tags,"LYRICS")==Values(tags,"LYRICS")&&Values(inspected.tags,"SPOTIFY_URI")==Values(tags,"SPOTIFY_URI"));
    const auto mvhd=Path(original,{"moov","mvhd"}),tkhd=Path(original,{"moov","trak","tkhd"}),mdhd=Path(original,{"moov","trak","mdia","mdhd"}),elst=Path(original,{"moov","trak","edts","elst"});
    assert(Read(original,mvhd+20)==44100&&Read(original,mvhd+24)==info.total_samples&&Read(original,tkhd+28)==info.total_samples);
    assert(Read(original,mdhd+24)==count*2048&&Read(original,elst+16)==info.total_samples&&Read(original,elst+20)==5119);

    // Unknown metadata is opaque and survives both ordinary deltas and an
    // approved complete replacement of the application's comment fields.
    const auto unknown=Atom("Xtra",{0xde,0xad,0xbe,0xef});const auto user=Atom("priv",{1,2,3});
    auto source=Insert(original,{"moov","udta","meta","ilst"},unknown);source=Insert(source,{"moov","udta"},user);Inspect(source);
    Tags delta;delta.fields={{"GENRE","Jazz"},{"LYRICS","[00:00.00] Updated"},{"TRACKTOTAL","11"}};Bytes tagged;
    assert(TagAacM4a(source,delta,tagged,error));SameAudio(source,tagged);auto updated=Inspect(tagged);
    assert(Values(updated.tags,"TITLE")==Values(tags,"TITLE")&&Values(updated.tags,"TRACKNUMBER")==std::vector<std::string>({"2"})&&Values(updated.tags,"TRACKTOTAL")==std::vector<std::string>({"11"}));
    assert(Values(updated.tags,"CUSTOM_REPEAT")==std::vector<std::string>({"one","two"})&&updated.tags.cover==tags.cover);
    assert(At(tagged,{"moov","udta","meta","ilst","Xtra"})==unknown&&At(tagged,{"moov","udta","priv"})==user);
    Tags replace;replace.replace_fields=true;replace.fields={{"TITLE","Approved"},{"CUSTOM_REPEAT","three"},{"CUSTOM_REPEAT","four"}};
    assert(TagAacM4a(source,replace,tagged,error));SameAudio(source,tagged);updated=Inspect(tagged);
    assert(Values(updated.tags,"ARTIST").empty()&&Values(updated.tags,"CUSTOM_REPEAT")==std::vector<std::string>({"three","four"})&&updated.tags.cover==tags.cover);
    assert(At(tagged,{"moov","udta","meta","ilst","Xtra"})==unknown&&At(tagged,{"moov","udta","priv"})==user);
    const auto mixed=Insert(source,{"moov","udta","meta","ilst","\251lyr"},Atom("extra",{7,8,9}));
    Tags unrelated;unrelated.fields={{"GENRE","Jazz"}};
    assert(TagAacM4a(mixed,unrelated,tagged,error));SameAudio(mixed,tagged);
    assert(At(tagged,{"moov","udta","meta","ilst","\251lyr"})==At(mixed,{"moov","udta","meta","ilst","\251lyr"}));
    assert(!TagAacM4a(mixed,delta,tagged,error)&&tagged.empty());
    Tags art;art.cover=artwork_fixture::jpeg();art.mime="image/jpeg";
    assert(TagAacM4a(source,art,tagged,error));SameAudio(source,tagged);updated=Inspect(tagged);assert(updated.tags.cover==art.cover&&updated.covers.size()==1);
    Bytes opaque_data;Append(opaque_data,99);Append(opaque_data,0);opaque_data.push_back(0xaa);const auto opaque=Atom("covr",Atom("data",opaque_data));
    auto opaque_source=Insert(source,{"moov","udta","meta","ilst"},opaque);auto opaque_info=Inspect(opaque_source);assert(opaque_info.has_artwork&&!opaque_info.artwork_edit_safe&&opaque_info.covers.size()==1);
    assert(TagAacM4a(opaque_source,delta,tagged,error));SameAudio(opaque_source,tagged);assert(!Inspect(tagged).artwork_edit_safe);
    assert(!TagAacM4a(opaque_source,art,tagged,error)&&tagged.empty());
    // Absent artwork is valid; no metadata change leaves bytes unchanged.
    Tags empty;assert(TagAacM4a(source,empty,tagged,error)&&tagged==source);
    Bytes plain;assert(MakeAacM4a(info,packets,empty,plain,error)&&!Inspect(plain).has_artwork);
    assert(TagAacM4a(plain,empty,tagged,error)&&tagged==plain); // No udta is synthesized.
    auto localized=source;const auto cover_data=Path(localized,{"moov","udta","meta","ilst","covr","data"});Write(localized,cover_data+12,1);
    assert(!Inspect(localized).artwork_edit_safe&&!TagAacM4a(localized,art,tagged,error)&&tagged.empty());

    // Fixed structural/configuration regressions, not fuzzing or stress runs.
    auto bad=original;bad.pop_back();Rejected(bad);
    bad=original;const auto stco=Path(bad,{"moov","trak","mdia","minf","stbl","stco"});Write(bad,stco+16,Read(bad,stco+16)+1);Rejected(bad);
    bad=original;const auto stsz=Path(bad,{"moov","trak","mdia","minf","stbl","stsz"});Write(bad,stsz+16,Read(bad,stsz+16)+1);Rejected(bad);
    bad=original;const auto stts=Path(bad,{"moov","trak","mdia","minf","stbl","stts"});Write(bad,stts+20,1024);Rejected(bad);
    bad=original;Write(bad,elst+20,5119+2048);Rejected(bad);
    bad=original;Write(bad,mvhd+24,info.total_samples-1);Rejected(bad);
    bad=Insert(original,{"moov"},At(original,{"moov","trak"}));Rejected(bad);
    bad=Insert(original,{"moov","trak","mdia","minf","stbl"},At(original,{"moov","trak","mdia","minf","stbl","stsz"}));Rejected(bad);
    auto unsupported=info;unsupported.config={0xf9,0x40,0x00};assert(!MakeAacM4a(unsupported,packets,tags,tagged,error)&&tagged.empty());
    unsupported=info;unsupported.config.resize(65);assert(!MakeAacM4a(unsupported,packets,tags,tagged,error));
    unsupported=info;unsupported.rate=48000;assert(!MakeAacM4a(unsupported,packets,tags,tagged,error));
    unsupported=info;unsupported.total_samples+=2048;assert(!MakeAacM4a(unsupported,packets,tags,tagged,error));
    auto short_packets=packets;short_packets.pop_back();assert(!MakeAacM4a(info,short_packets,tags,tagged,error));
    auto empty_packet=packets;empty_packet[0].clear();assert(!MakeAacM4a(info,empty_packet,tags,tagged,error));
    // Failure after the bounded mux has completed must still clean up fully.
    Tags invalid_art;invalid_art.cover={0,1,2};invalid_art.mime="image/png";
    assert(!MakeAacM4a(info,packets,invalid_art,tagged,error)&&tagged.empty());
    // A failed operation cannot poison the next bounded mux operation.
    assert(MakeAacM4a(info,packets,empty,tagged,error));Inspect(tagged);
    std::puts("PASS: bounded M4A remux, exact AAC bytes/config/timing, metadata preservation and conservative rejection");
    return 0;
}
#ifndef FLOGGFY_COMBINED_TEST
int main(){return RunAacContainerTests();}
#endif
