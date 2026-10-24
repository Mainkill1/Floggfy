#include "flac_history_core.h"
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
bool FlacCoverage::Frame(const FlacInfo& info,unsigned block,unsigned rate,unsigned channels,unsigned bits,unsigned number_type,uint64_t number) {
    if(!block || block>info.max_block || rate!=info.rate || channels!=info.channels || bits!=info.bits || number_type>1) return false;
    if(number_type==0 && (!info.max_block || number>info.total_samples/info.max_block)) return false;
    uint64_t position=number_type==1 ? number : number*info.max_block;
    if(position!=samples || !info.total_samples || samples>info.total_samples || block>info.total_samples-samples) return false;
    samples+=block; ++frames; return true;
}
static uint32_t Little(const uint8_t* p) { return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24); }
static void Number(std::vector<uint8_t>& out,uint32_t n) { for(unsigned i=0;i<4;i++) out.push_back(uint8_t(n>>(8*i))); }
static std::string Upper(std::string s) { for(char& c:s) if(c>='a' && c<='z') c=char(c-'a'+'A'); return s; }
static bool Fields(const uint8_t* b,size_t n,const Tags& tags,std::string& vendor,std::vector<std::string>& fields) {
    size_t p=0;
    auto read=[&](std::string& s) { if(n-p<4) return false; size_t len=Little(b+p); p+=4; if(len>n-p) return false; s.assign(reinterpret_cast<const char*>(b+p),len); p+=len; return true; };
    if(!read(vendor) || n-p<4) return false;
    uint32_t count=Little(b+p); p+=4;
    if(count>100000 || count>(n-p)/4) return false;
    for(uint32_t i=0;i<count;i++) {
        std::string s; if(!read(s)) return false; auto key=Upper(s.substr(0,s.find('=')));
        bool replace=tags.replace_fields || key=="METADATA_BLOCK_PICTURE";
        for(const auto& tag:tags.fields) if(key==Upper(tag.first)) replace=true;
        if(!replace) fields.push_back(std::move(s));
    }
    return p==n;
}
static bool CommentFields(const std::vector<uint8_t>& source,std::string& vendor,std::vector<std::string>& fields,size_t& audio) {
    FlacInfo info;if(ParseFlacMetadata(source,info,audio)!=FlacParse::Valid || source.size()-audio<2)return false;
    unsigned count=0;
    for(size_t p=4;p<audio;){
        unsigned kind=source[p]&127;size_t n=(size_t(source[p+1])<<16)|(size_t(source[p+2])<<8)|source[p+3];p+=4;
        if(kind==4){
            // Fields() removes legacy artwork comments during full retagging;
            // a metadata-only fill must instead preserve every original field.
            if(++count>1)return false;
            size_t q=0;auto read=[&](std::string& value){if(n-q<4)return false;size_t len=Little(source.data()+p+q);q+=4;if(len>n-q)return false;value.assign(reinterpret_cast<const char*>(source.data()+p+q),len);q+=len;return true;};
            if(!read(vendor)||n-q<4)return false;
            uint32_t entries=Little(source.data()+p+q);q+=4;if(entries>100000 || entries>(n-q)/4)return false;
            for(uint32_t i=0;i<entries;i++){std::string field;if(!read(field)||field.find('=')==std::string::npos)return false;fields.push_back(std::move(field));}
            if(q!=n)return false;
        }
        p+=n;
    }
    return true;
}
bool ReadFlacComments(const std::vector<uint8_t>& source,Tags& tags,std::string& error) {
    tags={};error.clear();std::string vendor;std::vector<std::string> fields;size_t audio=0;
    if(!CommentFields(source,vendor,fields,audio)){error="invalid FLAC comments";return false;}
    for(const auto& field:fields){auto split=field.find('=');tags.fields.push_back({Upper(field.substr(0,split)),field.substr(split+1)});}
    return true;
}
static bool Words(const std::string& s){return s.find_first_not_of(" \r\n\t")!=std::string::npos;}
static std::string Value(const Tags& tags,const char* key){for(const auto& field:tags.fields)if(Upper(field.first)==key&&Words(field.second))return field.second;return {};}
static bool SameTrack(const Tags& existing,const Tags& incoming) {
    const auto old_uri=Value(existing,"SPOTIFY_URI"),new_uri=Value(incoming,"SPOTIFY_URI");
    if(!old_uri.empty()&&!new_uri.empty()){
        if(old_uri!=new_uri)return false;
    }else{
        for(const char* key:{"TITLE","ARTIST","ALBUM"}){
            const auto value=Value(existing,key);if(value.empty()||value!=Value(incoming,key))return false;
        }
    }
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
bool CopyFlacLyrics(const std::vector<uint8_t>& source,Tags& incoming) {
    Tags existing;std::string error;if(!ReadFlacComments(source,existing,error)||!SameTrack(existing,incoming))return false;
    const auto lyrics=Value(existing,"LYRICS"),alias=Value(existing,"UNSYNCEDLYRICS");
    const auto old=!lyrics.empty()?lyrics:alias;
    const auto current=Value(incoming,"LYRICS");
    if(old.empty()||IsSyncedLyrics(current))return false;
    if((!current.empty()||!Value(incoming,"UNSYNCEDLYRICS").empty())&&!IsSyncedLyrics(old))return false;
    for(auto it=incoming.fields.begin();it!=incoming.fields.end();)if(Upper(it->first)=="LYRICS")it=incoming.fields.erase(it);else ++it;
    incoming.fields.push_back({!lyrics.empty()?"LYRICS":"UNSYNCEDLYRICS",old});return true;
}
LyricsUpdate FillFlacLyrics(const std::vector<uint8_t>& source,const Tags& incoming,std::vector<uint8_t>& output,std::string& error) {
    output.clear();error.clear();auto lyrics=Value(incoming,"LYRICS");
    if(lyrics.empty() || lyrics.size()>65536)return LyricsUpdate::Unchanged;
    Tags existing;if(!ReadFlacComments(source,existing,error))return LyricsUpdate::Invalid;
    const auto old=Value(existing,"LYRICS"),alias=Value(existing,"UNSYNCEDLYRICS");
    if((!old.empty()||!alias.empty())&&(!IsSyncedLyrics(lyrics)||IsSyncedLyrics(old)||IsSyncedLyrics(alias)))return LyricsUpdate::Unchanged;
    if(!SameTrack(existing,incoming))return LyricsUpdate::Unchanged;
    std::string vendor;std::vector<std::string> fields;size_t audio=0;
    if(!CommentFields(source,vendor,fields,audio))return LyricsUpdate::Invalid;
    // Remove only empty LYRICS fields, leaving aliases and all unrelated tags.
    std::vector<uint8_t> comments;Number(comments,uint32_t(vendor.size()));comments.insert(comments.end(),vendor.begin(),vendor.end());
    size_t count=1;for(const auto& field:fields)if(Upper(field.substr(0,field.find('=')))!="LYRICS")++count;
    Number(comments,uint32_t(count));
    for(const auto& field:fields)if(Upper(field.substr(0,field.find('=')))!="LYRICS"){Number(comments,uint32_t(field.size()));comments.insert(comments.end(),field.begin(),field.end());}
    const auto field="LYRICS="+lyrics;Number(comments,uint32_t(field.size()));comments.insert(comments.end(),field.begin(),field.end());
    if(comments.size()>0xffffff){error="FLAC comments too large";return LyricsUpdate::Invalid;}
    output={'f','L','a','C'};bool found=false;
    for(size_t p=4;p<audio;){
        const unsigned kind=source[p]&127;size_t n=(size_t(source[p+1])<<16)|(size_t(source[p+2])<<8)|source[p+3];
        if(kind==4){found=true;output.push_back(source[p]);output.push_back(uint8_t(comments.size()>>16));output.push_back(uint8_t(comments.size()>>8));output.push_back(uint8_t(comments.size()));output.insert(output.end(),comments.begin(),comments.end());}
        else {output.insert(output.end(),source.begin()+p,source.begin()+p+4+n);if(!found && p+4+n==audio)output[output.size()-n-4]&=127;}
        p+=4+n;
    }
    if(!found){output.insert(output.end(),{132,uint8_t(comments.size()>>16),uint8_t(comments.size()>>8),uint8_t(comments.size())});output.insert(output.end(),comments.begin(),comments.end());}
    output.insert(output.end(),source.begin()+audio,source.end());return LyricsUpdate::Updated;
}
bool TagFlac(const std::vector<uint8_t>& source,const Tags& tags,std::vector<uint8_t>& output,std::string& error) {
    output.clear(); error.clear();
    auto fail=[&](const char* why) { error=why; output.clear(); return false; };
    FlacInfo info; size_t audio;
    if(ParseFlacMetadata(source,info,audio)!=FlacParse::Valid || source.size()-audio<2) return fail("invalid or incomplete FLAC metadata/audio");
    std::vector<std::pair<unsigned,std::vector<uint8_t>>> blocks;
    std::vector<std::string> fields; std::string vendor="SpotifyRepair native history";
    std::vector<uint8_t> picture;
    const bool has_picture=BuildPicture(tags,picture);
    size_t p=4; unsigned comment_blocks=0;
    while(p<audio) {
        unsigned kind=source[p]&127; size_t n=(size_t(source[p+1])<<16)|(size_t(source[p+2])<<8)|source[p+3]; p+=4;
        if(kind==4) {
            if(++comment_blocks>1 || !Fields(source.data()+p,n,tags,vendor,fields)) return fail("invalid FLAC Vorbis comments");
        } else if(kind!=1 && (kind!=6 || !has_picture)) { // Preserve old artwork unless a valid replacement arrived.
            blocks.push_back({kind,std::vector<uint8_t>(source.begin()+p,source.begin()+p+n)});
        }
        p+=n;
    }
    for(const auto& tag:tags.fields) fields.push_back(tag.first+'='+tag.second);
    std::vector<uint8_t> comments; Number(comments,uint32_t(vendor.size())); comments.insert(comments.end(),vendor.begin(),vendor.end());
    Number(comments,uint32_t(fields.size()));
    for(const auto& field:fields) { Number(comments,uint32_t(field.size())); comments.insert(comments.end(),field.begin(),field.end()); }
    blocks.push_back({4,std::move(comments)});
    if(has_picture) blocks.push_back({6,std::move(picture)});
    output.reserve(source.size()+tags.cover.size()+65536); output={'f','L','a','C'};
    for(size_t i=0;i<blocks.size();i++) {
        const auto& block=blocks[i]; size_t n=block.second.size(); if(n>0xffffff) return fail("FLAC metadata block too large");
        output.push_back(uint8_t(block.first|(i+1==blocks.size()?128:0)));
        output.push_back(uint8_t(n>>16)); output.push_back(uint8_t(n>>8)); output.push_back(uint8_t(n));
        output.insert(output.end(),block.second.begin(),block.second.end());
    }
    output.insert(output.end(),source.begin()+audio,source.end()); return true;
}
}
