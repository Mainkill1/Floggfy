#include "aac_stream.h"
#include "image_info.h"
#include "vendor/minimp4/minimp4.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <iterator>
#include <set>
#include <utility>
#include <new>
#include <stdexcept>
extern "C" int floggfy_mp4_begin(void);
extern "C" void floggfy_mp4_end(void);
namespace history {
namespace {
using Bytes=std::vector<uint8_t>;
constexpr size_t file_limit=512u*1024u*1024u,metadata_limit=16u*1024u*1024u;
constexpr uint32_t packet_limit=1u<<20;
constexpr uint32_t Code(char a,char b,char c,char d) {
    return (uint32_t(uint8_t(a))<<24)|(uint32_t(uint8_t(b))<<16)|(uint32_t(uint8_t(c))<<8)|uint8_t(d);
}
constexpr uint32_t moov=Code('m','o','o','v'),trak=Code('t','r','a','k'),udta=Code('u','d','t','a');
constexpr uint32_t meta=Code('m','e','t','a'),ilst=Code('i','l','s','t'),covr=Code('c','o','v','r');
struct InvalidFailure {};
struct ResourceFailure { const char* reason; };
[[noreturn]] void Bad(){throw InvalidFailure{};}
void Need(bool value){if(!value)Bad();}
void Resource(bool value,const char* reason){if(!value)throw ResourceFailure{reason};}
constexpr uint64_t vendor_memory=uint64_t{128}<<20,scratch_base=uint64_t{8}<<20;
constexpr uint64_t allocation_overhead=32;
// Every charge is checked before arithmetic; a rejected estimate allocates nothing.
bool Charge(uint64_t& total,uint64_t count,uint64_t each=1) noexcept {
    if(total>AacConstructionMemoryLimit||count>(AacConstructionMemoryLimit-total)/each)return false;
    total+=count*each;return true;
}
void AddCharge(uint64_t& total,uint64_t count,uint64_t each=1){
    Resource(Charge(total,count,each),"AAC resource limit: retained input or construction peak exceeds 512 MiB");
}
uint64_t MetadataBound(uint64_t bytes,uint64_t fields){
    Resource(bytes<=metadata_limit&&fields<=65536,"AAC resource limit: metadata exceeds supported budget");
    uint64_t bound=4096;AddCharge(bound,bytes);AddCharge(bound,fields,128);return bound;
}
struct TagMemory {uint64_t retained=sizeof(Tags),bytes=0,fields=0;};
TagMemory MeasureTags(const Tags& tags){
    TagMemory out;out.fields=tags.fields.size();
    Resource(out.fields<=65536&&tags.cover.size()<=4u*1024u*1024u,"AAC resource limit: metadata exceeds supported budget");
    AddCharge(out.retained,tags.fields.capacity(),sizeof(tags.fields[0]));
    AddCharge(out.retained,tags.cover.capacity());AddCharge(out.retained,tags.mime.capacity());
    AddCharge(out.retained,2,allocation_overhead);AddCharge(out.bytes,tags.cover.size());
    for(const auto& field:tags.fields){
        Need(field.first.size()<=255);
        AddCharge(out.bytes,field.first.size());AddCharge(out.bytes,field.second.size());
        AddCharge(out.retained,field.first.capacity());AddCharge(out.retained,field.second.capacity());
        AddCharge(out.retained,2,allocation_overhead);
    }
    Resource(out.bytes<=metadata_limit,"AAC resource limit: metadata exceeds supported budget");
    return out;
}
uint16_t U16(const uint8_t* p){return uint16_t((uint16_t(p[0])<<8)|p[1]);}
uint32_t U32(const uint8_t* p){return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];}
uint64_t U64(const uint8_t* p){return (uint64_t(U32(p))<<32)|U32(p+4);}
void W32(Bytes& b,size_t at,uint32_t n){Need(at<=b.size()&&b.size()-at>=4);for(unsigned i=0;i<4;++i)b[at+i]=uint8_t(n>>(24-i*8));}
void Add(Bytes& b,const uint8_t* p,size_t n){Need(b.size()<=file_limit&&n<=file_limit-b.size());if(n)b.insert(b.end(),p,p+n);}
void Add(Bytes& b,const Bytes& more){Add(b,more.data(),more.size());}
void N32(Bytes& b,uint32_t n){for(unsigned i=0;i<4;++i)b.push_back(uint8_t(n>>(24-i*8)));}
void N16(Bytes& b,uint16_t n){b.push_back(uint8_t(n>>8));b.push_back(uint8_t(n));}
Bytes Boxed(uint32_t kind,const Bytes& payload){Need(payload.size()<=file_limit-8);Bytes b;b.reserve(payload.size()+8);N32(b,uint32_t(payload.size()+8));N32(b,kind);Add(b,payload);return b;}
struct Box {size_t at=0,size=0,header=0;uint32_t kind=0;size_t Body()const{return at+header;}size_t End()const{return at+size;}};
std::vector<Box> Boxes(const uint8_t* b,size_t first,size_t end){
    std::vector<Box> result;
    while(first<end){
        Need(end-first>=8&&result.size()<65536);
        uint64_t n=U32(b+first);const auto kind=U32(b+first+4);size_t h=8;
        if(n==1){Need(end-first>=16);n=U64(b+first+8);h=16;}
        Need(n>=h&&n<=end-first);result.push_back({first,size_t(n),h,kind});first+=size_t(n);
    }
    return result;
}
const Box* Optional(const std::vector<Box>& boxes,uint32_t kind){const Box* found=nullptr;for(const auto& x:boxes)if(x.kind==kind){Need(!found);found=&x;}return found;}
Box One(const std::vector<Box>& boxes,uint32_t kind){const auto* b=Optional(boxes,kind);Need(b);return *b;}
std::vector<Box> Children(const uint8_t* b,const Box& box,size_t skip=0){Need(skip<=box.size-box.header);return Boxes(b,box.Body()+skip,box.End());}
Bytes Copy(const uint8_t* b,const Box& box){return Bytes(b+box.at,b+box.End());}
void Full(const uint8_t* b,const Box& box,size_t minimum=4){Need(box.size-box.header>=minimum&&U32(b+box.Body())==0);}
struct Bits {
    const Bytes& b;size_t at=0;
    uint32_t Get(unsigned n){Need(n<=24&&at+n<=b.size()*8);uint32_t v=0;while(n--){v=(v<<1)|((b[at/8]>>(7-at%8))&1);++at;}return v;}
    size_t Left()const{return b.size()*8-at;}
};
uint32_t Rate(Bits& bits){static const uint32_t rates[]={96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350};const auto index=bits.Get(4);Need(index<13||index==15);return index==15?bits.Get(24):rates[index];}
void Configuration(AacInfo& info){
    Need(info.config.size()>=2&&info.config.size()<=64);Bits bits{info.config};
    auto aot=bits.Get(5);Need(aot==2||aot==5||aot==29);
    const auto core=Rate(bits);const auto channels=bits.Get(4);Need(channels==1||channels==2);
    uint32_t output=core;bool ps=aot==29,sbr=aot==5||ps;
    if(sbr){output=Rate(bits);aot=bits.Get(5);Need(aot==2);}
    const auto frame=bits.Get(1)?960u:1024u;Need(bits.Get(1)==0&&bits.Get(1)==0);
    // Support the standard backward-compatible SBR/PS sync extensions too.
    if(!sbr&&bits.Left()>=16){
        const auto saved=bits.at;
        if(bits.Get(11)==0x2b7){Need(bits.Get(5)==5);sbr=bits.Get(1)!=0;if(sbr)output=Rate(bits);
            if(bits.Left()>=12){const auto extension=bits.at;if(bits.Get(11)==0x548)ps=bits.Get(1)!=0;else bits.at=extension;}
        }else bits.at=saved;
    }
    while(bits.Left())Need(bits.Get(1)==0);
    Need(core>=8000&&core<=96000&&output>=8000&&output<=96000&&(output==core||output==core*2));
    Need(!ps||(sbr&&channels==1));
    info.rate=output;info.channels=ps?2:channels;info.samples_per_packet=frame*(output/core);
}
struct Descriptor{uint8_t kind;size_t body,end;};
Descriptor Desc(const uint8_t* b,size_t& at,size_t end){Need(at<end);const auto kind=b[at++];uint32_t size=0;unsigned count=0;uint8_t v;
    do{Need(at<end&&count++<4);v=b[at++];size=(size<<7)|(v&127);}while(v&128);
    Need(size<=end-at);Descriptor d{kind,at,at+size};at=d.end;return d;
}
Bytes ReadConfig(const uint8_t* b,const Box& box){
    Full(b,box,9);size_t at=box.Body()+4;const auto es=Desc(b,at,box.End());Need(es.kind==3&&at==box.End()&&es.end-es.body>=3&&b[es.body+2]==0);
    at=es.body+3;const auto dc=Desc(b,at,es.end);Need(dc.kind==4&&dc.end-dc.body>=13&&b[dc.body]==0x40&&b[dc.body+1]==0x15);
    size_t di=dc.body+13;const auto config=Desc(b,di,dc.end);Need(config.kind==5&&di==dc.end&&config.end-config.body<=64);
    const auto sl=Desc(b,at,es.end);Need(sl.kind==6&&sl.end-sl.body==1&&b[sl.body]==2&&at==es.end);
    return Bytes(b+config.body,b+config.end);
}
bool Text(const std::string& s){
    for(size_t i=0;i<s.size();){uint32_t c=uint8_t(s[i++]);if(c<128){if(!c)return false;continue;}unsigned more;uint32_t minimum;
        if(c>=0xc2&&c<=0xdf){more=1;minimum=128;c&=31;}else if(c>=0xe0&&c<=0xef){more=2;minimum=2048;c&=15;}else if(c>=0xf0&&c<=0xf4){more=3;minimum=65536;c&=7;}else return false;
        if(more>s.size()-i)return false;
        while(more--){const auto v=uint8_t(s[i++]);if((v&192)!=128)return false;c=(c<<6)|(v&63);}if(c<minimum||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return false;
    }return true;
}
struct Mapping{const char* key;uint32_t kind;};
constexpr Mapping mappings[]={{"TITLE",Code('\251','n','a','m')},{"ARTIST",Code('\251','A','R','T')},{"ALBUMARTIST",Code('a','A','R','T')},{"ALBUM",Code('\251','a','l','b')},{"DATE",Code('\251','d','a','y')},{"GENRE",Code('\251','g','e','n')},{"COMMENT",Code('\251','c','m','t')},{"COMPOSER",Code('\251','w','r','t')},{"COPYRIGHT",Code('c','p','r','t')},{"LYRICS",Code('\251','l','y','r')},{"DESCRIPTION",Code('d','e','s','c')}};
std::string Key(std::string key){for(auto& c:key)if(c>='a'&&c<='z')c=char(c-'a'+'A');return key;}
std::string String(const uint8_t* b,size_t begin,size_t end){return std::string(reinterpret_cast<const char*>(b+begin),end-begin);}
struct Item{Box box;std::vector<std::pair<std::string,std::string>> fields;bool edit_safe=true;};
struct Metadata{std::vector<Item> items;Box user{},metadata{},list{};};
Metadata ReadTags(const uint8_t* b,const Box& movie,AacFile& file){
    Metadata result;const auto children=Children(b,movie);const auto* user=Optional(children,udta);if(!user)return result;result.user=*user;
    Need(user->size<=metadata_limit);const auto users=Children(b,*user);const auto* m=Optional(users,meta);if(!m)return result;result.metadata=*m;Full(b,*m);
    const auto metas=Children(b,*m,4);const auto* list=Optional(metas,ilst);if(!list)return result;result.list=*list;
    uint64_t best_area=0;
    for(const auto& entry:Children(b,*list)){
        Item item{entry,{}};
        const bool known=entry.kind==covr||entry.kind==Code('-','-','-','-')||entry.kind==Code('t','r','k','n')||entry.kind==Code('d','i','s','k')||std::any_of(std::begin(mappings),std::end(mappings),[&](const Mapping& mapping){return mapping.kind==entry.kind;});
        if(!known){result.items.push_back(std::move(item));continue;}
        const auto parts=Children(b,entry);
        if(entry.kind==covr){
            file.has_artwork=true;bool any=false;
            for(const auto& data:parts){
                if(data.kind!=Code('d','a','t','a')||data.size-data.header<8){file.artwork_edit_safe=false;continue;}
                any=true;const auto type=U32(b+data.Body());const auto n=data.End()-data.Body()-8;
                if(U32(b+data.Body()+4)!=0||(type!=13&&type!=14)||!n||n>4u*1024u*1024u){file.artwork_edit_safe=false;continue;}
                Bytes cover(b+data.Body()+8,b+data.End());ImageInfo image;
                if(!ReadImageInfo(cover,image)||image.mime!=(type==13?"image/jpeg":"image/png")){file.artwork_edit_safe=false;continue;}
                file.covers.push_back(cover);const auto area=uint64_t(image.width)*image.height;
                if(area>best_area){best_area=area;file.tags.cover=std::move(cover);file.tags.mime=image.mime;}
            }
            if(!any)file.artwork_edit_safe=false;
        }else{
            std::string key;for(const auto& mapping:mappings)if(entry.kind==mapping.kind)key=mapping.key;
            if(entry.kind==Code('-','-','-','-')){
                const auto* mean=Optional(parts,Code('m','e','a','n'));const auto* name=Optional(parts,Code('n','a','m','e'));
                if(mean&&name&&mean->size-mean->header>=4&&name->size-name->header>=4&&U32(b+mean->Body())==0&&U32(b+name->Body())==0&&String(b,mean->Body()+4,mean->End())=="com.apple.iTunes"){
                    key=Key(String(b,name->Body()+4,name->End()));if(key.size()>255||!Text(key))key.clear();
                }
            }
            const bool track=entry.kind==Code('t','r','k','n'),disc=entry.kind==Code('d','i','s','k');
            for(const auto& data:parts){
                if(entry.kind==Code('-','-','-','-')&&(data.kind==Code('m','e','a','n')||data.kind==Code('n','a','m','e')))continue;
                if(data.kind!=Code('d','a','t','a')||data.size-data.header<8){item.edit_safe=false;continue;}
                const auto type=U32(b+data.Body());const auto start=data.Body()+8;const auto n=data.End()-start;
                if(U32(b+data.Body()+4)!=0){item.edit_safe=false;continue;}
                if((track||disc)&&type==0&&(n==6||(n==8&&U16(b+start+6)==0))&&U16(b+start)==0){
                    item.fields.emplace_back(track?"TRACKNUMBER":"DISCNUMBER",std::to_string(U16(b+start+2)));
                    item.fields.emplace_back(track?"TRACKTOTAL":"DISCTOTAL",std::to_string(U16(b+start+4)));
                }else if(!key.empty()&&type==1){
                    auto value=String(b,start,data.End());if(Text(value))item.fields.emplace_back(key,std::move(value));else item.edit_safe=false;
                }else item.edit_safe=false;
            }
        }
        for(const auto& field:item.fields)file.tags.fields.push_back(field);
        result.items.push_back(std::move(item));
    }
    return result;
}
// Validate the top-level byte ranges without building a box vector. This runs
// before inspection/rewrite allocations so opaque, huge moov payloads cannot
// evade the construction-memory policy.
Box TerminalMovie(const uint8_t* source,size_t size){
    Need(source&&size>=8);
    Resource(size<=file_limit,"AAC resource limit: file exceeds 512 MiB");
    Box movie;size_t at=0,count=0;
    while(at<size){
        Need(size-at>=8&&count++<65536);
        uint64_t bytes=U32(source+at);size_t header=8;
        if(bytes==1){Need(size-at>=16);bytes=U64(source+at+8);header=16;}
        Need(bytes>=header&&bytes<=size-at);
        if(U32(source+at+4)==moov){Need(!movie.size);movie={at,size_t(bytes),header,moov};}
        at+=size_t(bytes);
    }
    Need(movie.size&&movie.End()==size);return movie;
}
uint64_t RewriteMemory(size_t source_capacity,size_t source_size,size_t movie_bytes,const TagMemory& tags,bool copy_media){
    const auto metadata=MetadataBound(tags.bytes,tags.fields);
    uint64_t output=source_size;AddCharge(output,metadata);
    Resource(output<=file_limit,"AAC resource limit: rewritten file exceeds 512 MiB");
    uint64_t peak=tags.retained;AddCharge(peak,source_capacity);
    if(copy_media)AddCharge(peak,output);
    AddCharge(peak,scratch_base);AddCharge(peak,movie_bytes,32);AddCharge(peak,metadata,32);
    AddCharge(peak,tags.fields,512);
    return output;
}
struct Layout{Box movie,track,movie_header,track_header,media_header;Metadata metadata;uint32_t packets=0;};
Layout Inspect(const uint8_t* b,size_t size,AacFile& file,bool require_edit=true){
    Need(b&&size>=8&&size<=file_limit);const auto top=Boxes(b,0,size);const auto ft=One(top,Code('f','t','y','p'));Need(ft.at==0&&ft.size-ft.header>=8&&(ft.size-ft.header)%4==0);
    bool brand=false;for(size_t at=ft.Body();at<ft.End();at+=4)if(at!=ft.Body()+4){const auto value=U32(b+at);if(value==Code('i','s','o','m')||value==Code('m','p','4','2')||value==Code('M','4','A',' '))brand=true;}Need(brand);
    const auto media=One(top,Code('m','d','a','t'));const auto movie=One(top,moov);Need(movie.End()==size&&media.End()<=movie.at);
    Need(!Optional(top,Code('m','o','o','f')));
    Layout out;out.movie=movie;const auto movies=Children(b,movie);out.track=One(movies,trak);out.movie_header=One(movies,Code('m','v','h','d'));Need(!Optional(movies,Code('m','v','e','x')));
    Full(b,out.movie_header,100);const auto movie_rate=U32(b+out.movie_header.Body()+12),movie_duration=U32(b+out.movie_header.Body()+16);
    const auto tracks=Children(b,out.track);out.track_header=One(tracks,Code('t','k','h','d'));Need(out.track_header.size-out.track_header.header==84&&b[out.track_header.Body()]==0);
    const auto track_duration=U32(b+out.track_header.Body()+20);const auto mdia=One(tracks,Code('m','d','i','a'));const auto mdias=Children(b,mdia);out.media_header=One(mdias,Code('m','d','h','d'));Full(b,out.media_header,24);
    const auto rate=U32(b+out.media_header.Body()+12),decoded=U32(b+out.media_header.Body()+16);Need(rate>=8000&&rate<=96000&&decoded);
    const auto handler=One(mdias,Code('h','d','l','r'));Full(b,handler,24);Need(U32(b+handler.Body()+8)==Code('s','o','u','n'));
    const auto minf=One(mdias,Code('m','i','n','f'));const auto minfs=Children(b,minf);const auto dinf=One(minfs,Code('d','i','n','f'));const auto dref=One(Children(b,dinf),Code('d','r','e','f'));Full(b,dref,8);Need(U32(b+dref.Body()+4)==1);
    const auto url=One(Children(b,dref,8),Code('u','r','l',' '));Need(url.size-url.header==4&&U32(b+url.Body())==1);
    const auto stbl=One(minfs,Code('s','t','b','l'));const auto tables=Children(b,stbl);
    Need(!Optional(tables,Code('c','t','t','s'))&&!Optional(tables,Code('s','t','z','2'))&&!Optional(tables,Code('s','t','s','s')));
    const auto stsd=One(tables,Code('s','t','s','d'));Full(b,stsd,8);Need(U32(b+stsd.Body()+4)==1);const auto entries=Children(b,stsd,8);Need(entries.size()==1&&entries[0].kind==Code('m','p','4','a'));
    const auto sample=entries[0];Need(sample.size-sample.header>=28&&U16(b+sample.Body()+6)==1&&U16(b+sample.Body()+8)==0);
    const auto sample_children=Children(b,sample,28);Need(!Optional(sample_children,Code('s','i','n','f')));
    file.info.config=ReadConfig(b,One(sample_children,Code('e','s','d','s')));Configuration(file.info);Need(file.info.rate==rate&&U16(b+sample.Body()+16)==file.info.channels);
    auto legacy_rate=rate;while(legacy_rate>65535)legacy_rate>>=1;Need(U32(b+sample.Body()+24)==legacy_rate<<16);
    const auto stsz=One(tables,Code('s','t','s','z'));Full(b,stsz,12);Need(U32(b+stsz.Body()+4)==0);const auto count=U32(b+stsz.Body()+8);Need(count&&count<=packet_limit&&stsz.size-stsz.header==12+size_t(count)*4);out.packets=count;
    const auto stsc=One(tables,Code('s','t','s','c'));Full(b,stsc,20);Need(stsc.size-stsc.header==20&&U32(b+stsc.Body()+4)==1&&U32(b+stsc.Body()+8)==1&&U32(b+stsc.Body()+12)==1&&U32(b+stsc.Body()+16)==1);
    const auto stco=One(tables,Code('s','t','c','o'));Full(b,stco,8);Need(!Optional(tables,Code('c','o','6','4'))&&U32(b+stco.Body()+4)==count&&stco.size-stco.header==8+size_t(count)*4);
    uint64_t at=media.Body();for(uint32_t i=0;i<count;++i){const auto offset=U32(b+stco.Body()+8+size_t(i)*4),length=U32(b+stsz.Body()+12+size_t(i)*4);Need(length&&offset==at&&length<=media.End()-at);at+=length;}Need(at==media.End());file.audio_bytes=media.size-media.header;
    const auto stts=One(tables,Code('s','t','t','s'));Full(b,stts,16);Need(stts.size-stts.header==16&&U32(b+stts.Body()+4)==1&&U32(b+stts.Body()+8)==count&&U32(b+stts.Body()+12)==file.info.samples_per_packet);
    Need(uint64_t(count)*file.info.samples_per_packet==decoded);
    if(require_edit){
        Need(movie_rate==rate);const auto edit=One(tracks,Code('e','d','t','s'));const auto edits=Children(b,edit);Need(edits.size()==1);const auto elst=One(edits,Code('e','l','s','t'));Full(b,elst,20);
        Need(elst.size-elst.header==20&&U32(b+elst.Body()+4)==1&&U32(b+elst.Body()+16)==0x10000);
        file.info.total_samples=U32(b+elst.Body()+8);file.info.priming=U32(b+elst.Body()+12);
        Need(file.info.total_samples&&file.info.priming<=uint32_t(INT32_MAX)&&uint64_t(file.info.total_samples)<=uint64_t(rate)*6*3600&&movie_duration==file.info.total_samples&&track_duration==movie_duration);
        const uint64_t used=uint64_t(file.info.total_samples)+file.info.priming;Need(used<=decoded&&decoded-used<file.info.samples_per_packet);
    }
    out.metadata=ReadTags(b,movie,file);file.complete=require_edit;return out;
}
Bytes Rebox(const uint8_t* source,const Box& old,uint32_t kind,uint32_t replace,const Bytes& replacement,size_t prefix=0){
    Bytes payload;bool replaced=false;
    if(old.size){Need(prefix<=old.size-old.header);Add(payload,source+old.Body(),prefix);const auto children=Children(source,old,prefix);Optional(children,replace);
        for(const auto& child:children){if(child.kind==replace){Add(payload,replacement);replaced=true;}else Add(payload,source+child.at,child.size);}
    }else payload.resize(prefix);
    if(!replaced)Add(payload,replacement);
    return Boxed(kind,payload);
}
Bytes Data(uint32_t type,const Bytes& value){Bytes b;N32(b,type);N32(b,0);Add(b,value);return Boxed(Code('d','a','t','a'),b);}
Bytes ValueBytes(const std::string& value){return Bytes(value.begin(),value.end());}
Bytes FieldItem(const std::string& key,const std::string& value){
    Need(!key.empty()&&key.size()<=255&&Text(key)&&Text(value));
    uint32_t kind=Code('-','-','-','-');for(const auto& mapping:mappings)if(key==mapping.key)kind=mapping.kind;
    Bytes payload;
    if(kind==Code('-','-','-','-')){Bytes mean(4),name(4);Add(mean,ValueBytes("com.apple.iTunes"));Add(name,ValueBytes(key));Add(payload,Boxed(Code('m','e','a','n'),mean));Add(payload,Boxed(Code('n','a','m','e'),name));}
    Add(payload,Data(1,ValueBytes(value)));return Boxed(kind,payload);
}
uint16_t Number(const std::string& value){if(value.empty())return 0;uint32_t n=0;for(const auto c:value){Need(c>='0'&&c<='9');n=n*10+uint32_t(c-'0');Need(n<=65535);}return uint16_t(n);}
std::string Field(const std::vector<std::pair<std::string,std::string>>& fields,const std::string& key){for(const auto& f:fields)if(f.first==key)return f.second;return {};}
Bytes MetadataMovie(const uint8_t* source,const Layout& layout,const AacFile& file,const Tags& delta){
    Resource(delta.fields.size()<=65536&&delta.cover.size()<=4u*1024u*1024u,"AAC resource limit: metadata exceeds supported budget");
    std::vector<std::pair<std::string,std::string>> fields;std::set<std::string> touched;size_t total=delta.cover.size();
    for(const auto& field:delta.fields){Need(field.first.size()<=255&&field.second.size()<=metadata_limit-total);total+=field.first.size()+field.second.size();Need(total<=metadata_limit);const auto key=Key(field.first);Need(!key.empty()&&Text(key)&&Text(field.second));fields.emplace_back(key,field.second);touched.insert(key);}
    const bool track=touched.count("TRACKNUMBER")||touched.count("TRACKTOTAL"),disc=touched.count("DISCNUMBER")||touched.count("DISCTOTAL");
    if(track){touched.insert("TRACKNUMBER");touched.insert("TRACKTOTAL");}if(disc){touched.insert("DISCNUMBER");touched.insert("DISCTOTAL");}
    // A partial numeric delta keeps the untouched half of the standard pair.
    for(const char* key:{"TRACKNUMBER","TRACKTOTAL","DISCNUMBER","DISCTOTAL"})if(!delta.replace_fields&&touched.count(key)&&std::none_of(fields.begin(),fields.end(),[&](const auto& f){return f.first==key;}))fields.emplace_back(key,Field(file.tags.fields,key));
    const bool cover=!delta.cover.empty();if(cover)Need(file.artwork_edit_safe);
    Bytes entries;
    for(const auto& item:layout.metadata.items){
        bool remove=cover&&item.box.kind==covr;
        if(!item.fields.empty()){if(delta.replace_fields)remove=true;else for(const auto& field:item.fields)if(touched.count(field.first))remove=true;}
        // A mixed text item may also carry unknown/binary/localized children.
        // Refuse its replacement instead of silently discarding those siblings.
        if(remove)Need(item.edit_safe);
        else Add(entries,source+item.box.at,item.box.size);
    }
    for(const auto& field:fields)if(field.first!="TRACKNUMBER"&&field.first!="TRACKTOTAL"&&field.first!="DISCNUMBER"&&field.first!="DISCTOTAL")Add(entries,FieldItem(field.first,field.second));
    for(const bool is_disc:{false,true}){
        const char* number=is_disc?"DISCNUMBER":"TRACKNUMBER";const char* maximum=is_disc?"DISCTOTAL":"TRACKTOTAL";
        if(std::any_of(fields.begin(),fields.end(),[&](const auto& f){return f.first==number||f.first==maximum;})){
            Bytes value;N16(value,0);N16(value,Number(Field(fields,number)));N16(value,Number(Field(fields,maximum)));if(!is_disc)N16(value,0);
            Add(entries,Boxed(is_disc?Code('d','i','s','k'):Code('t','r','k','n'),Data(0,value)));
        }
    }
    if(cover){ImageInfo image;Need(ReadImageInfo(delta.cover,image)&&image.mime==delta.mime&&(image.mime=="image/png"||image.mime=="image/jpeg"));Add(entries,Boxed(covr,Data(image.mime=="image/png"?14:13,delta.cover)));}
    Need(entries.size()<=metadata_limit);
    auto metadata=Rebox(source,layout.metadata.metadata,meta,ilst,Boxed(ilst,entries),4);
    if(!layout.metadata.metadata.size){
        Bytes handler(8);N32(handler,Code('m','d','i','r'));handler.resize(25);
        Bytes content(4);Add(content,Boxed(Code('h','d','l','r'),handler));Add(content,Boxed(ilst,entries));metadata=Boxed(meta,content);
    }
    const auto user=Rebox(source,layout.metadata.user,udta,meta,metadata);
    return Rebox(source,layout.movie,moov,udta,user);
}
Bytes TimedMovie(const uint8_t* b,const Layout& layout,const AacInfo& info){
    auto movie_header=Copy(b,layout.movie_header),track_header=Copy(b,layout.track_header);
    W32(movie_header,layout.movie_header.header+12,info.rate);W32(movie_header,layout.movie_header.header+16,info.total_samples);W32(track_header,layout.track_header.header+20,info.total_samples);
    Bytes edit(4);N32(edit,1);N32(edit,info.total_samples);N32(edit,info.priming);N32(edit,0x10000);
    const auto edts=Boxed(Code('e','d','t','s'),Boxed(Code('e','l','s','t'),edit));
    Bytes track_payload;for(const auto& child:Children(b,layout.track)){if(child.kind==Code('t','k','h','d'))Add(track_payload,track_header);else{Need(child.kind!=Code('e','d','t','s'));Add(track_payload,b+child.at,child.size);}}Add(track_payload,edts);
    const auto track=Boxed(trak,track_payload);Bytes movie_payload;
    for(const auto& child:Children(b,layout.movie)){if(child.kind==trak)Add(movie_payload,track);else if(child.kind==Code('m','v','h','d'))Add(movie_payload,movie_header);else Add(movie_payload,b+child.at,child.size);}
    return Boxed(moov,movie_payload);
}
// Make owns this single reserved media buffer. Vendor writes and later moov
// replacements must remain within the preflight bound; neither may grow it.
struct Sink {Bytes bytes;size_t limit=0;bool failed=false;};
int Write(int64_t offset,const void* data,size_t size,void* context)noexcept{
    auto& sink=*static_cast<Sink*>(context);
    try{
        if(sink.failed||offset<0||uint64_t(offset)>sink.limit||size>sink.limit-size_t(offset)){
            sink.failed=true;return -1;
        }
        const auto end=size_t(offset)+size;
        if(end>sink.bytes.capacity()){sink.failed=true;return -1;}
        if(end>sink.bytes.size())sink.bytes.resize(end);
        if(size)std::memcpy(sink.bytes.data()+size_t(offset),data,size);
        return 0;
    }catch(...){sink.failed=true;return -1;}
}
struct Budget {
    Budget(){Resource(floggfy_mp4_begin()!=0,"AAC resource limit: vendor allocation budget unavailable");}
    ~Budget(){floggfy_mp4_end();}
};
struct Mux {
    MP4E_mux_t* handle=nullptr;
    ~Mux(){if(handle)MP4E_close(handle);}
    int Close(){auto* h=handle;handle=nullptr;return MP4E_close(h);}
};
void MuxResult(int status,const Sink& sink){
    Resource(status!=MP4E_STATUS_NO_MEMORY&&!sink.failed,"AAC resource limit: mux allocation or output budget exhausted");
    Need(status==MP4E_STATUS_OK);
}
void ReplaceMovie(Sink& sink,const Box& old,const Bytes& replacement){
    Need(old.kind==moov&&old.End()==sink.bytes.size()&&replacement.size()>=8&&
         U32(replacement.data())==replacement.size()&&U32(replacement.data()+4)==moov);
    Resource(old.at<=sink.limit&&replacement.size()<=sink.limit-old.at,
             "AAC resource limit: terminal moov exceeds reserved output");
    const size_t end=old.at+replacement.size();
    Resource(end<=sink.bytes.capacity(),"AAC resource limit: terminal moov requires another media allocation");
    sink.bytes.resize(end);
    std::memcpy(sink.bytes.data()+old.at,replacement.data(),replacement.size());
}
void Failure(AacWriteFailure* out,AacWriteFailure value){if(out)*out=value;}
}
bool EstimateAacConstructionMemory(uint64_t packet_count,uint64_t packet_bytes,
    uint64_t retained_input_bytes,uint64_t metadata_bytes,uint64_t metadata_fields,
    AacMemoryPlan& plan) noexcept {
    plan={};
    if(!packet_count||packet_count>packet_limit||packet_bytes<packet_count||
       packet_bytes>file_limit||retained_input_bytes<packet_bytes||
       metadata_bytes>metadata_limit||metadata_fields>65536)return false;
    // Each AU contributes stsz+stco entries. 8 KiB covers all fixed boxes,
    // exact ASC, edit-list and metadata envelopes; each text field receives
    // 128 bytes of descriptor/key wrapping beyond its raw bytes.
    uint64_t movie=8192;
    if(!Charge(movie,packet_count,8)||!Charge(movie,metadata_bytes)||
       !Charge(movie,metadata_fields,128))return false;
    uint64_t output=packet_bytes;
    if(!Charge(output,movie)||!Charge(output,64)||output>file_limit)return false;
    // 32x moov bounds simultaneously live boxed copies, vector growth and
    // inspection fields/art. Extra per-field space covers string/set objects.
    // Keep the full vendor allowance even though it is freed before rewriting.
    uint64_t peak=retained_input_bytes;
    if(!Charge(peak,vendor_memory)||!Charge(peak,output)||!Charge(peak,scratch_base)||
       !Charge(peak,movie,32)||!Charge(peak,metadata_fields,512))return false;
    plan={peak,output};return true;
}
bool InspectAacM4a(const uint8_t* source,size_t size,AacFile& out){
    out={};
    try{
        const auto movie=TerminalMovie(source,size);
        uint64_t peak=size;AddCharge(peak,scratch_base);AddCharge(peak,movie.size,32);
        AacFile result;Inspect(source,size,result);out=std::move(result);return true;
    }catch(...){return false;}
}
bool TagAacM4a(const Bytes& source,const Tags& tags,Bytes& output,std::string& error,AacWriteFailure* failure){
    Failure(failure,AacWriteFailure::None);error.clear();
    if(&source==&output||&tags.cover==&output){
        error="M4A output must not alias its source or metadata";
        Failure(failure,AacWriteFailure::InvalidOrUnsupported);return false;
    }
    Bytes().swap(output);
    try{
        const auto terminal=TerminalMovie(source.data(),source.size());
        const auto metadata=MeasureTags(tags);
        const auto capacity=RewriteMemory(source.capacity(),source.size(),terminal.size,metadata,true);
        AacFile file;const auto layout=Inspect(source.data(),source.size(),file);
        if(tags.fields.empty()&&tags.cover.empty()&&!tags.replace_fields){output=source;return true;}
        const auto movie=MetadataMovie(source.data(),layout,file,tags);
        Resource(layout.movie.at<=capacity&&movie.size()<=capacity-layout.movie.at,
                 "AAC resource limit: metadata rewrite exceeded planned output");
        // External tagging preserves its caller's source, so this path needs
        // exactly one additional media buffer; its full capacity was budgeted.
        Bytes result;result.reserve(size_t(capacity));
        Add(result,source.data(),layout.movie.at);Add(result,movie);
        AacFile checked;Inspect(result.data(),result.size(),checked);
        output=std::move(result);return true;
    }catch(const ResourceFailure& problem){
        error=problem.reason;Failure(failure,AacWriteFailure::ResourceLimit);
    }catch(const std::bad_alloc&){
        error="AAC resource limit: allocation failed";Failure(failure,AacWriteFailure::ResourceLimit);
    }catch(const std::length_error&){
        error="AAC resource limit: allocation size rejected";Failure(failure,AacWriteFailure::ResourceLimit);
    }catch(...){
        error="Unsupported or invalid M4A metadata update";Failure(failure,AacWriteFailure::InvalidOrUnsupported);
    }
    Bytes().swap(output);return false;
}
bool MakeAacM4a(const AacInfo& info,const std::vector<Bytes>& packets,const Tags& tags,
               Bytes& output,std::string& error,AacWriteFailure* failure){
    Failure(failure,AacWriteFailure::None);error.clear();
    bool alias=&output==&info.config||&output==&tags.cover;
    for(const auto& packet:packets)if(&packet==&output)alias=true;
    if(alias){
        error="M4A output must not alias input access units, configuration or metadata";
        Failure(failure,AacWriteFailure::InvalidOrUnsupported);return false;
    }
    Bytes().swap(output);
    try{
        Need(!packets.empty());
        Resource(packets.size()<=packet_limit,"AAC resource limit: too many access units");
        Need(info.config.size()<=64);
        const auto metadata=MeasureTags(tags);
        uint64_t retained=metadata.retained,bytes=0;
        AddCharge(retained,sizeof(info)+sizeof(packets)+allocation_overhead);
        AddCharge(retained,info.config.capacity());
        AddCharge(retained,packets.capacity(),sizeof(Bytes));
        AddCharge(retained,packets.size(),allocation_overhead);
        for(const auto& packet:packets){
            Need(!packet.empty()&&packet.size()<=65536);
            AddCharge(bytes,packet.size());AddCharge(retained,packet.capacity());
        }
        AacMemoryPlan plan;
        Resource(EstimateAacConstructionMemory(packets.size(),bytes,retained,metadata.bytes,metadata.fields,plan),
                 "AAC resource limit: planned construction peak exceeds 512 MiB");
        // All potentially large input/output/scratch capacities were checked
        // above; only the <=64-byte decoder configuration is copied here.
        AacInfo parsed;parsed.config=info.config;Configuration(parsed);
        Need(parsed.rate==info.rate&&parsed.channels==info.channels&&parsed.samples_per_packet==info.samples_per_packet);
        Need(info.total_samples&&uint64_t(info.total_samples)<=uint64_t(info.rate)*6*3600&&info.priming<=uint32_t(INT32_MAX));
        const uint64_t samples=packets.size()*uint64_t(info.samples_per_packet),used=uint64_t(info.total_samples)+info.priming;
        Need(samples<=UINT32_MAX&&samples>=used&&samples-used<info.samples_per_packet);
        Sink sink;sink.limit=size_t(plan.output_capacity);sink.bytes.reserve(sink.limit);
        Resource(sink.bytes.capacity()==sink.limit,"AAC resource limit: allocator exceeded planned output capacity");
        {
            Budget budget;Mux mux;mux.handle=MP4E_open(0,0,&sink,Write);
            Resource(mux.handle!=nullptr,"AAC resource limit: mux allocation failed");
            MP4E_track_t track{};track.object_type_indication=MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3;
            track.track_media_kind=e_audio;track.time_scale=info.rate;track.default_duration=info.samples_per_packet;
            track.u.a.channelcount=info.channels;track.language[0]='u';track.language[1]='n';track.language[2]='d';
            const int id=MP4E_add_track(mux.handle,&track);
            if(id<0)MuxResult(id,sink);
            Need(id==0);
            MuxResult(MP4E_set_dsi(mux.handle,id,info.config.data(),int(info.config.size())),sink);
            for(const auto& packet:packets)MuxResult(MP4E_put_sample(mux.handle,id,packet.data(),int(packet.size()),int(info.samples_per_packet),MP4E_SAMPLE_RANDOM_ACCESS),sink);
            MuxResult(mux.Close(),sink);
        } // Free the vendor arena before any moov reconstruction.
        {
            AacFile initial;const auto layout=Inspect(sink.bytes.data(),sink.bytes.size(),initial,false);
            const auto movie=TimedMovie(sink.bytes.data(),layout,info);ReplaceMovie(sink,layout.movie,movie);
        }
        if(!tags.fields.empty()||!tags.cover.empty()||tags.replace_fields){
            AacFile file;const auto layout=Inspect(sink.bytes.data(),sink.bytes.size(),file);
            const auto movie=MetadataMovie(sink.bytes.data(),layout,file,tags);ReplaceMovie(sink,layout.movie,movie);
        }
        AacFile checked;Inspect(sink.bytes.data(),sink.bytes.size(),checked);
        Need(checked.info.config==info.config&&checked.info.rate==info.rate&&checked.info.channels==info.channels&&
             checked.info.samples_per_packet==info.samples_per_packet&&checked.info.priming==info.priming&&
             checked.info.total_samples==info.total_samples&&checked.audio_bytes==bytes);
        output=std::move(sink.bytes);return true;
    }catch(const ResourceFailure& problem){
        error=problem.reason;Failure(failure,AacWriteFailure::ResourceLimit);
    }catch(const std::bad_alloc&){
        error="AAC resource limit: allocation failed";Failure(failure,AacWriteFailure::ResourceLimit);
    }catch(const std::length_error&){
        error="AAC resource limit: allocation size rejected";Failure(failure,AacWriteFailure::ResourceLimit);
    }catch(...){
        error="Unsupported AAC configuration, timing, or M4A structure";Failure(failure,AacWriteFailure::InvalidOrUnsupported);
    }
    Bytes().swap(output);return false;
}
}
