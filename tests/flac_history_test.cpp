#include "../native/flac_history_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace history;
static void check(bool v,const char* label) { if(!v) { fprintf(stderr,"FAIL: %s\n",label); exit(1); } }
static std::vector<uint8_t> source() {
    std::vector<uint8_t> b={'f','L','a','C',128,0,0,34}; b.resize(42);
    b[8]=0x10; b[10]=0x10; // fixed block size 4096
    uint64_t packed=(uint64_t(44100)<<44)|(uint64_t(1)<<41)|(uint64_t(15)<<36)|8192;
    for(unsigned i=0;i<8;i++) b[18+i]=uint8_t(packed>>(56-8*i));
    b.insert(b.end(),{255,248,1,2,3,4,5,6}); return b;
}
int main(int argc,char**argv) {
    auto b=source(); FlacInfo info; size_t audio=0;
    check(ParseFlacMetadata(b,info,audio)==FlacParse::Valid && audio==42 && info.rate==44100 && info.channels==2 && info.bits==16 && info.total_samples==8192,"STREAMINFO layout and metadata end");
    auto partial=b; partial.resize(30); check(ParseFlacMetadata(partial,info,audio)==FlacParse::NeedMore,"partial metadata stays in RAM");
    auto bad=b; bad[7]=33; check(ParseFlacMetadata(bad,info,audio)==FlacParse::Invalid,"invalid STREAMINFO rejected");
    ParseFlacMetadata(b,info,audio);
    FlacCoverage c;
    check(c.Frame(info,4096,44100,2,16,0,0) && !c.Complete(info),"first fixed-block decoder frame");
    check(c.Frame(info,4096,44100,2,16,0,1) && c.Complete(info),"contiguous sample coverage reaches total");
    FlacCoverage gap; check(!gap.Frame(info,4096,44100,2,16,0,1),"missing first frame rejected");
    FlacCoverage variable; check(variable.Frame(info,4096,44100,2,16,1,0) && !variable.Frame(info,4096,44100,2,16,1,5000),"variable-block seek/gap rejected");
    Tags tags; tags.fields={{"TITLE","Track"},{"ARTIST","Artist"},{"ALBUM","Album"}};
    tags.cover={137,80,78,71,13,10,26,10}; tags.mime="image/png";
    std::vector<uint8_t> out; std::string error;
    check(TagFlac(b,tags,out,error),"native FLAC tag/picture construction");
    FlacInfo tagged; size_t start;
    check(ParseFlacMetadata(out,tagged,start)==FlacParse::Valid && tagged.total_samples==info.total_samples,"STREAMINFO samples preserved");
    check(out.size()-start==b.size()-audio && !memcmp(out.data()+start,b.data()+audio,b.size()-audio),"compressed audio frame bytes unchanged");
    unsigned comments=0,pictures=0; size_t p=4;
    while(p<start) { unsigned kind=out[p]&127; size_t n=(size_t(out[p+1])<<16)|(size_t(out[p+2])<<8)|out[p+3]; comments+=kind==4; pictures+=kind==6; p+=4+n; }
    check(comments==1 && pictures==1,"one native VORBIS_COMMENT and PICTURE block");
    auto duplicate=out; check(TagFlac(duplicate,tags,out,error),"retagging replaces metadata rather than stacking");
    if(argc==4) {
        FILE* f=fopen(argv[1],"rb"); check(f,"real source opens"); fseek(f,0,SEEK_END); b.resize(ftell(f)); rewind(f); check(fread(b.data(),1,b.size(),f)==b.size(),"real source read"); fclose(f);
        f=fopen(argv[2],"rb"); check(f,"real artwork opens"); fseek(f,0,SEEK_END); tags.cover.resize(ftell(f)); rewind(f); fread(tags.cover.data(),1,tags.cover.size(),f); fclose(f);
        check(TagFlac(b,tags,out,error),"real source native tagging"); f=fopen(argv[3],"wb"); check(f,"output opens"); check(fwrite(out.data(),1,out.size(),f)==out.size(),"output write"); fclose(f);
    }
    puts("PASS: FLAC metadata, contiguous decoded-frame coverage, native embedded art/tags and original frame preservation");
}
