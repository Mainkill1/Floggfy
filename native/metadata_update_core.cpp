#include "metadata_update_core.h"
#include "aac_stream.h"
#include "flac_history_core.h"
#include <algorithm>
#include <set>
namespace history {
namespace {
constexpr size_t comment_limit=16*1024*1024;
std::string Upper(std::string text) {
    for(char& c:text)if(c>='a' && c<='z')c=char(c-'a'+'A');
    return text;
}
bool Words(const std::string& value) {return value.find_first_not_of(" \r\n\t")!=std::string::npos;}
std::string Value(const Tags& tags,const std::string& key) {
    for(const auto& field:tags.fields)if(Upper(field.first)==key && Words(field.second))return field.second;
    return {};
}
bool TextKey(const std::string& key) {
    if(key.empty())return false;
    for(unsigned char c:key)if(c<32 || c>125 || c=='=')return false;
    return true;
}
std::string Year(const std::string& date) {
    if(date.size()<4)return {};
    for(unsigned i=0;i<4;++i)if(date[i]<'0' || date[i]>'9')return {};
    return date.substr(0,4)=="0000" ? std::string{} : date.substr(0,4);
}
void ReleaseDateDelta(const Tags& existing,const Tags& incoming,Tags& delta) {
    const auto old_date=Value(existing,"DATE"),old_year=Value(existing,"YEAR");
    auto date=old_date;
    if(date.empty()) {
        const auto candidate=Value(incoming,"DATE");
        if(!candidate.empty() && (old_year.empty() || (!Year(candidate).empty() && Year(candidate)==Year(old_year)))) {
            date=candidate;delta.fields.emplace_back("DATE",candidate);
        }
    }
    if(old_year.empty()) {
        auto year=Year(old_date);
        if(year.empty())year=Year(date);
        if(year.empty())year=Year(Value(incoming,"YEAR"));
        if(!year.empty())delta.fields.emplace_back("YEAR",year);
    }
}
void LyricsDelta(const Tags& existing,const Tags& incoming,Tags& delta) {
    bool old_words=false;
    for(const auto& field:existing.fields) {
        const auto key=Upper(field.first);
        if(key!="LYRICS" && key!="UNSYNCEDLYRICS")continue;
        if(Words(field.second))old_words=true;
        if(IsSyncedLyrics(field.second))return;
    }
    std::string lyrics;
    for(const char* key:{"LYRICS","UNSYNCEDLYRICS"})for(const auto& field:incoming.fields) {
        if(Upper(field.first)!=key || !Words(field.second) || field.second.size()>65536)continue;
        if(lyrics.empty() || IsSyncedLyrics(field.second))lyrics=field.second;
        if(IsSyncedLyrics(lyrics))break;
    }
    if(!lyrics.empty() && (!old_words || IsSyncedLyrics(lyrics)))delta.fields.emplace_back("LYRICS",std::move(lyrics));
}
constexpr const char* artwork_source="FLOGGFY_ARTWORK_SOURCE";
bool BetterArtwork(const RecordingFile& existing,const Tags& incoming) {
    if(!existing.artwork_edit_safe)return false;
    RecordingArtwork candidate;
    if(!ReadRecordingArtwork(incoming.cover,candidate) || candidate.mime!=incoming.mime)return false;
    auto identical=[&](const RecordingArtwork& old){return old.valid && old.bytes==candidate.bytes && old.digest==candidate.digest;};
    if(identical(existing.artwork))return false;
    for(const auto& old:existing.artworks)if(identical(old))return false;
    if(!existing.artwork.valid)return true;
    if(Value(incoming,artwork_source)!="spotify-client")return false;
    auto smaller=[&](const RecordingArtwork& old){return old.valid && (candidate.width<old.width || candidate.height<old.height);};
    if(smaller(existing.artwork))return false;
    for(const auto& old:existing.artworks)if(smaller(old))return false;
    if(candidate.width>existing.artwork.width || candidate.height>existing.artwork.height)return true;
    return Value(incoming,artwork_source)=="spotify-client" && Value(existing.tags,artwork_source)!="spotify-client";
}
struct MetadataPlan {
    Tags delta;
    bool picture=false;
    bool Needed() const {return picture || !delta.fields.empty();}
};
MetadataPlan PlanMetadata(const RecordingFile& existing,const Tags& incoming) {
    MetadataPlan plan;
    if(!existing.complete || !existing.owned)return plan;
    auto& delta=plan.delta;std::set<std::string> populated;
    for(const auto& field:existing.tags.fields)if(Words(field.second))populated.insert(Upper(field.first));
    for(const auto& field:incoming.fields) {
        const auto key=Upper(field.first);
        if(!TextKey(key) || !Words(field.second) || key.rfind("HISTORY_",0)==0 || key=="METADATA_BLOCK_PICTURE" ||
           key==artwork_source || key=="LYRICS" || key=="UNSYNCEDLYRICS" || key=="YEAR" || key=="DATE" || populated.count(key))continue;
        delta.fields.emplace_back(key,field.second);
    }
    ReleaseDateDelta(existing.tags,incoming,delta);LyricsDelta(existing.tags,incoming,delta);
    plan.picture=BetterArtwork(existing,incoming);
    if(plan.picture) {
        const auto source=Value(incoming,artwork_source);
        if(source=="spotify-client")delta.fields.emplace_back(artwork_source,source);
        else if(Words(Value(existing.tags,artwork_source)))delta.fields.emplace_back(artwork_source,"");
    }
    return plan;
}
uint32_t Little(const uint8_t* bytes) {
    return uint32_t(bytes[0])|(uint32_t(bytes[1])<<8)|(uint32_t(bytes[2])<<16)|(uint32_t(bytes[3])<<24);
}
void Number(std::vector<uint8_t>& bytes,uint32_t value) {
    for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(value>>(i*8)));
}
void Block(std::vector<uint8_t>& output,uint8_t type,const std::vector<uint8_t>& body) {
    output.push_back(type);output.push_back(uint8_t(body.size()>>16));output.push_back(uint8_t(body.size()>>8));output.push_back(uint8_t(body.size()));
    output.insert(output.end(),body.begin(),body.end());
}
bool FillFlac(const std::vector<uint8_t>& source,const Tags& delta,const std::vector<uint8_t>& picture,
              std::vector<uint8_t>& output,std::string& error) {
    FlacInfo info;size_t audio=0;
    if(ParseFlacMetadata(source,info,audio)!=FlacParse::Valid){error="invalid FLAC metadata";return false;}
    std::string vendor;std::vector<std::string> fields;bool found=false;
    for(size_t p=4;p<audio;) {
        const auto type=source[p]&127;const size_t size=(size_t(source[p+1])<<16)|(size_t(source[p+2])<<8)|source[p+3];p+=4;
        if(type==4) {
            if(found){error="duplicate FLAC comment blocks";return false;}found=true;
            size_t at=0;
            auto read=[&](std::string& text) {
                if(size-at<4)return false;
                const auto length=Little(source.data()+p+at);at+=4;
                if(length>size-at)return false;
                text.assign(reinterpret_cast<const char*>(source.data()+p+at),length);at+=length;return true;
            };
            if(!read(vendor) || size-at<4){error="invalid FLAC comments";return false;}
            const auto count=Little(source.data()+p+at);at+=4;
            if(count>100000 || count>(size-at)/4){error="invalid FLAC comment count";return false;}
            for(uint32_t i=0;i<count;++i){std::string field;if(!read(field)){error="invalid FLAC comment field";return false;}fields.push_back(std::move(field));}
            if(at!=size){error="trailing FLAC comment bytes";return false;}
        }
        p+=size;
    }
    if(!found){error="owned recording has no comments";return false;}
    std::set<std::string> replaced;
    for(const auto& field:delta.fields)replaced.insert(Upper(field.first));
    if(!picture.empty())replaced.insert("METADATA_BLOCK_PICTURE");
    fields.erase(std::remove_if(fields.begin(),fields.end(),[&](const auto& field) {
        const auto split=field.find('=');return split!=std::string::npos && replaced.count(Upper(field.substr(0,split)));
    }),fields.end());
    for(const auto& field:delta.fields)fields.push_back(field.first+'='+field.second);
    std::vector<uint8_t> comments;Number(comments,uint32_t(vendor.size()));comments.insert(comments.end(),vendor.begin(),vendor.end());
    if(fields.size()>100000){error="too many FLAC comments";return false;}Number(comments,uint32_t(fields.size()));
    for(const auto& field:fields) {
        if(field.size()>comment_limit || comments.size()>comment_limit-field.size() || comment_limit-comments.size()-field.size()<4){error="FLAC comments too large";return false;}
        Number(comments,uint32_t(field.size()));comments.insert(comments.end(),field.begin(),field.end());
    }
    if(comments.size()>0xffffff || picture.size()>0xffffff){error="FLAC metadata block too large";return false;}
    output.assign(source.begin(),source.begin()+4);
    for(size_t p=4;p<audio;) {
        const auto type=source[p]&127;const size_t size=(size_t(source[p+1])<<16)|(size_t(source[p+2])<<8)|source[p+3];
        // Only accepted front-cover upgrades remove old PICTURE blocks. If
        // the old last block is a picture, its replacement keeps the last bit;
        // otherwise insert before the original last block, preserving padding.
        if((source[p]&128) && !picture.empty())Block(output,uint8_t(6|(type==6?128:0)),picture);
        if(type==4)Block(output,source[p],comments);
        else if(type!=6 || picture.empty())output.insert(output.end(),source.begin()+p,source.begin()+p+4+size);
        p+=4+size;
    }
    output.insert(output.end(),source.begin()+audio,source.end());return true;
}
bool SameQuality(const Quality& a,const Quality& b) {
    return a.codec==b.codec && a.rate==b.rate && a.channels==b.channels && a.bits==b.bits && a.bitrate==b.bitrate;
}
// Copy opaque FLAC PICTURE payloads, including booklets or images beyond our
// inspection budget. Failure to assess an old image never authorizes its loss.
bool FlacPictures(const std::vector<uint8_t>& source,std::vector<std::vector<uint8_t>>& pictures) {
    FlacInfo info;
    size_t audio=0;
    if(ParseFlacMetadata(source,info,audio)!=FlacParse::Valid)return false;
    for(size_t at=4;at<audio;) {
        const unsigned type=source[at]&127;
        const size_t length=(size_t(source[at+1])<<16)|(size_t(source[at+2])<<8)|source[at+3];
        at+=4;
        if(type==6)pictures.emplace_back(source.begin()+at,source.begin()+at+length);
        at+=length;
    }
    return true;
}
bool ReplaceFlacPictures(const std::vector<uint8_t>& incoming,
                         const std::vector<std::vector<uint8_t>>& pictures,std::vector<uint8_t>& source) {
    FlacInfo info;
    size_t audio=0;
    if(ParseFlacMetadata(incoming,info,audio)!=FlacParse::Valid)return false;
    source.assign(incoming.begin(),incoming.begin()+4);
    size_t last=0;
    for(size_t at=4;at<audio;) {
        const unsigned type=incoming[at]&127;
        const size_t length=(size_t(incoming[at+1])<<16)|(size_t(incoming[at+2])<<8)|incoming[at+3];
        if(type!=6) {
            last=source.size();
            source.insert(source.end(),incoming.begin()+at,incoming.begin()+at+4+length);
            source[last]&=127;
        }
        at+=4+length;
    }
    for(const auto& picture:pictures) {
        last=source.size();
        Block(source,6,picture);
    }
    if(!last)return false;
    source[last]|=128;
    source.insert(source.end(),incoming.begin()+audio,incoming.end());
    return true;
}
}
bool NeedsRecordingMetadata(const RecordingFile& existing,const Tags& incoming) {return PlanMetadata(existing,incoming).Needed();}
MetadataUpdate FillRecordingMetadata(const std::vector<uint8_t>& source,const Tags& incoming,
    const Catalog& catalog,std::vector<uint8_t>& output,std::string& error) {
    output.clear();error.clear();RecordingFile existing;
    if(!InspectRecording(source.data(),source.size(),existing) || !existing.complete || !existing.owned || !MatchesRecording(existing,catalog)) {
        error="recording is not complete, owned and identity matched";return MetadataUpdate::Invalid;
    }
    auto plan=PlanMetadata(existing,incoming);
    if(!plan.Needed())return MetadataUpdate::Unchanged;
    auto& delta=plan.delta;std::vector<uint8_t> picture;
    if(plan.picture) {
        if(existing.quality.codec!=Codec::Aac && !BuildPicture(incoming,picture)){error="invalid artwork candidate";return MetadataUpdate::Invalid;}
        delta.cover=incoming.cover;delta.mime=incoming.mime;
    }
    const bool updated=existing.quality.codec==Codec::Flac ? FillFlac(source,delta,picture,output,error) :
        existing.quality.codec==Codec::Aac ? TagAacM4a(source,delta,output,error) : TagOgg(source,delta,output,error);
    RecordingFile result;
    if(!updated || !InspectRecording(output.data(),output.size(),result) || !result.complete || !result.owned ||
       !MatchesRecording(result,catalog) || !SameQuality(existing.quality,result.quality)) {
        if(error.empty())error="updated recording validation failed";
        output.clear();return MetadataUpdate::Invalid;
    }
    return MetadataUpdate::Updated;
}
bool PreserveUpgradeMetadata(const std::vector<uint8_t>& source,const std::vector<uint8_t>& incoming,
    const Catalog& catalog,std::vector<uint8_t>& output,std::string& error) {
    output.clear();error.clear();
    RecordingFile old,next;
    if(!InspectRecording(source.data(),source.size(),old) || !old.complete || !old.owned ||
       !MatchesRecording(old,catalog) || !InspectRecording(incoming.data(),incoming.size(),next) ||
       !next.complete || !next.owned || !MatchesRecording(next,catalog) || old.quality.codec!=next.quality.codec) {
        error="upgrade metadata requires complete owned matching recordings";
        return false;
    }
    const auto plan=PlanMetadata(old,next.tags);
    if(next.quality.codec==Codec::Aac) {
        error="AAC renditions do not have a verified audio quality upgrade order";
        return false;
    }
    Tags merged;
    merged.replace_fields=true;
    // Preserve populated user fields (including repeats). The shared delta
    // fills blanks, keeps synchronized lyrics and resolves DATE/YEAR conflicts.
    std::set<std::string> changed;
    for(const auto& field:plan.delta.fields)changed.insert(Upper(field.first));
    for(const auto& field:old.tags.fields) {
        const auto key=Upper(field.first);
        if(key.rfind("HISTORY_",0)!=0 && key!="METADATA_BLOCK_PICTURE" && !changed.count(key))
            merged.fields.push_back(field);
    }
    merged.fields.insert(merged.fields.end(),plan.delta.fields.begin(),plan.delta.fields.end());
    for(const auto& field:next.tags.fields)
        if(Upper(field.first).rfind("HISTORY_",0)==0)merged.fields.push_back(field);

    // Legacy picture comments are retained too; they can be opaque or use a
    // format we cannot compare. A valid new front cover only replaces them
    // when the shared artwork policy explicitly authorizes that replacement.
    if(!plan.picture)for(const auto& picture:old.picture_comments)
        merged.fields.emplace_back("METADATA_BLOCK_PICTURE",picture);

    // Already-approved incoming metadata needs no second retagging pass. Keep
    // vendor text/packet layout intact as well when neither file has artwork.
    if(!old.has_artwork && !next.has_artwork && merged.fields==next.tags.fields) {
        output=incoming;
        return true;
    }

    bool tagged=false;
    if(next.quality.codec==Codec::Flac) {
        std::vector<std::vector<uint8_t>> pictures;
        if(plan.picture) {
            std::vector<uint8_t> picture;
            if(!BuildPicture(next.tags,picture))return false;
            pictures.push_back(std::move(picture));
        } else if(!FlacPictures(source,pictures))return false;
        std::vector<uint8_t> prepared;
        if(!ReplaceFlacPictures(incoming,pictures,prepared))return false;
        tagged=TagFlac(prepared,merged,output,error);
    } else {
        if(plan.picture) {
            merged.cover=next.tags.cover;
            merged.mime=next.tags.mime;
        }
        // replace_fields drops unapproved incoming art even when the old song
        // had none. Optional artwork must not prevent a valid audio upgrade.
        tagged=TagOgg(incoming,merged,output,error);
    }
    RecordingFile result;
    if(!tagged || !InspectRecording(output.data(),output.size(),result) || !result.complete ||
       !result.owned || !MatchesRecording(result,catalog) || !SameQuality(result.quality,next.quality)) {
        if(error.empty())error="preserved upgrade validation failed";
        output.clear();return false;
    }
    return true;
}
}
