#include "flac_history_core.h"
#include "tag_memory_plan.h"
#include "vorbis_comments.h"
#include <cstring>
namespace history {
static bool StreamInfo(const uint8_t* b,FlacInfo& info) {
            info.min_block=(unsigned(b[0])<<8)|b[1]; info.max_block=(unsigned(b[2])<<8)|b[3];
            uint64_t packed=0; for(unsigned i=10;i<18;i++) packed=(packed<<8)|b[i];
            info.rate=unsigned(packed>>44); info.channels=unsigned((packed>>41)&7)+1;
            info.bits=unsigned((packed>>36)&31)+1; info.total_samples=packed&0xfffffffffULL;
            if(!info.rate || info.rate>655350 || info.bits<4 || info.min_block<16 || info.max_block<info.min_block) return false;
    return true;
}
FlacParse ParseFlacStreamInfo(const uint8_t* source,size_t length,FlacInfo& info) {
    if(length<4)return FlacParse::NeedMore;
    if(!source || memcmp(source,"fLaC",4))return FlacParse::Invalid;
    if(length<8)return FlacParse::NeedMore;
    if((source[4]&127)!=0 || source[5]!=0 || source[6]!=0 || source[7]!=34)return FlacParse::Invalid;
    if(length<42)return FlacParse::NeedMore;
    return StreamInfo(source+8,info)?FlacParse::Valid:FlacParse::Invalid;
}
template<class Source> static FlacParse Metadata(const Source& source,FlacInfo& info,size_t& audio) {
    audio=0;
    if(source.size()<4) return FlacParse::NeedMore;
    for(unsigned i=0;i<4;i++) if(source[i]!=uint8_t("fLaC"[i])) return FlacParse::Invalid;
    size_t p=4; bool first=true;
    for(;;) {
        if(p>16*1024*1024) return FlacParse::Invalid;
        if(source.size()-p<4) return FlacParse::NeedMore;
        unsigned kind=source[p]&127; bool last=(source[p]&128)!=0;
        size_t n=(size_t(source[p+1])<<16)|(size_t(source[p+2])<<8)|source[p+3];
        if(kind==127 || (first && (kind!=0 || n!=34)) || (!first && kind==0)) return FlacParse::Invalid;
        p+=4;
        if(n>source.size()-p) return FlacParse::NeedMore;
        if(first) {
            uint8_t b[34]; for(unsigned i=0;i<34;i++) b[i]=source[p+i];
            if(!StreamInfo(b,info)) return FlacParse::Invalid;
            first=false;
        }
        p+=n;
        if(last) { audio=p; return FlacParse::Valid; }
    }
}
FlacParse ParseFlacMetadata(const std::vector<uint8_t>& source,FlacInfo& info,size_t& audio) { return Metadata(source,info,audio); }
FlacParse ParseFlacMetadata(const CompressedBuffer& source,FlacInfo& info,size_t& audio) { return Metadata(source,info,audio); }
bool FlacCoverage::Frame(const FlacInfo& info,unsigned block,unsigned rate,unsigned channels,unsigned bits,unsigned number_type,uint64_t number,const char** reason) {
    if(reason) *reason=nullptr;
    const auto reject=[reason](const char* detail) {
        if(reason) *reason=detail;
        return false;
    };
    // These reasons describe our capture guard, not a native decoder error.
    // Keep every rejection before mutating coverage or subtracting samples.
    if(!block || block>info.max_block) return reject("block-size");
    if(rate!=info.rate || channels!=info.channels || bits!=info.bits) return reject("format");
    if(number_type>1) return reject("number-type");
    if(number_type==0 && (!info.max_block || number>info.total_samples/info.max_block))
        return reject("frame-number");
    const uint64_t position=number_type==1 ? number : number*info.max_block;
    if(position!=samples) return reject("sample-position");
    if(!info.total_samples || samples>info.total_samples || block>info.total_samples-samples)
        return reject("sample-count");
    samples+=block;
    ++frames;
    return true;
}
bool IsSyncedLyrics(const std::string& words) {
    size_t p=0;unsigned previous=0,lines=0;
    while(p<words.size()) {
        if(words[p++]!='[')return false;
        unsigned minutes=0,digits=0;while(p<words.size()&&words[p]>='0'&&words[p]<='9'){if(++digits>4)return false;minutes=minutes*10+unsigned(words[p++]-'0');}
        if(digits<2||p+7>=words.size()||words[p++]!=':')return false;
        auto digit=[&](){if(p>=words.size()||words[p]<'0'||words[p]>'9')return -1;return int(words[p++]-'0');};
        int a=digit(),b=digit();if(a<0||b<0||a*10+b>=60||p>=words.size()||words[p++]!='.')return false;
        int c=digit(),d=digit();if(c<0||d<0)return false;
        unsigned fraction=unsigned(c*100+d*10);
        if(p<words.size()&&words[p]>='0'&&words[p]<='9')fraction+=unsigned(digit());
        if(p>=words.size()||words[p++]!=']')return false;
        unsigned time=minutes*60000+unsigned(a*10+b)*1000+fraction;
        if(time>86400000||(lines&&time<previous))return false;
        previous=time;++lines;
        auto newline=words.find('\n',p);p=newline==std::string::npos?words.size():newline+1;
    }
    return lines>0;
}
void AppendFlacBlock(std::vector<uint8_t>& output,uint8_t type,const std::vector<uint8_t>& body) {
    output.push_back(type);
    output.push_back(uint8_t(body.size()>>16));output.push_back(uint8_t(body.size()>>8));output.push_back(uint8_t(body.size()));
    output.insert(output.end(),body.begin(),body.end());
}
bool TagFlacWithPictures(const std::vector<uint8_t>& source,const Tags& tags,std::vector<uint8_t>& output,
                         std::string& error,const std::vector<std::vector<uint8_t>>* approved_pictures) {
    output.clear();error.clear();
    auto fail=[&](const char* why){error=why;output.clear();return false;};
    FlacInfo info;size_t audio;
    if(ParseFlacMetadata(source,info,audio)!=FlacParse::Valid || source.size()-audio<2)return fail("invalid or incomplete FLAC metadata/audio");
    TagMemoryPlan plan;
    if(!PlanTagConstructionMemory(Codec::Flac,source.size(),audio,0,tags,plan))return fail("FLAC tag allocation overflow");
    output.reserve(plan.output_capacity);output={'f','L','a','C'};
    std::vector<uint8_t> picture;
    const bool has_picture=!approved_pictures && BuildPicture(tags,picture);
    VorbisComments comments;
    bool found=false;
    for(size_t at=4;at<audio;) {
        const uint8_t kind=source[at]&127;
        const size_t length=(size_t(source[at+1])<<16)|(size_t(source[at+2])<<8)|source[at+3];
        if(kind==4) {
            if(found || !comments.Read(source.data()+at+4,length,tags,true))return fail("invalid FLAC Vorbis comments");
            found=true;
        } else if(kind!=1 && (kind!=6 || (!approved_pictures && !has_picture))) {
            output.push_back(kind);
            output.insert(output.end(),source.begin()+at+1,source.begin()+at+4+length);
        }
        at+=4+length;
    }
    if(approved_pictures)for(const auto& block:*approved_pictures) {
        if(block.size()>0xffffff)return fail("FLAC metadata block too large");
        AppendFlacBlock(output,6,block);
    }
    std::vector<uint8_t> body;
    if(!comments.Write(body,tags,0xffffff) || picture.size()>0xffffff)return fail("FLAC metadata block too large");
    AppendFlacBlock(output,has_picture ? 4 : 128|4,body);
    if(has_picture)AppendFlacBlock(output,128|6,picture);
    output.insert(output.end(),source.begin()+audio,source.end());
    return true;
}
bool TagFlac(const std::vector<uint8_t>& source,const Tags& tags,std::vector<uint8_t>& output,std::string& error) {
    return TagFlacWithPictures(source,tags,output,error,nullptr);
}
}
