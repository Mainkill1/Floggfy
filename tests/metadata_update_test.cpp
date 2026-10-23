#include "../native/metadata_update_core.h"
#include "../native/flac_history_core.h"
#include "recording_fixture.h"
#include "artwork_fixture.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
using namespace history;
namespace {
std::string Upper(std::string value) {for(char& c:value)if(c>='a'&&c<='z')c=char(c-'a'+'A');return value;}
std::vector<std::string> Values(const Tags& tags,const std::string& key) {
    std::vector<std::string> values;for(const auto& field:tags.fields)if(Upper(field.first)==key)values.push_back(field.second);return values;
}
std::string Value(const Tags& tags,const std::string& key) {
    for(const auto& field:tags.fields)if(Upper(field.first)==key&&!field.second.empty())return field.second;
    return {};
}
void Set(Tags& tags,const std::string& key,const std::string& value) {
    tags.fields.erase(std::remove_if(tags.fields.begin(),tags.fields.end(),[&](const auto& field){return Upper(field.first)==key;}),tags.fields.end());
    if(!value.empty())tags.fields.emplace_back(key,value);
}
uint32_t Little(const uint8_t* bytes) {return uint32_t(bytes[0])|(uint32_t(bytes[1])<<8)|(uint32_t(bytes[2])<<16)|(uint32_t(bytes[3])<<24);}
std::vector<std::vector<uint8_t>> Packets(const std::vector<uint8_t>& source) {
    std::vector<std::vector<uint8_t>> packets;std::vector<uint8_t> packet;
    for(size_t p=0;p<source.size();) {
        const size_t header=27+source[p+26];size_t at=p+header;
        for(size_t i=p+27;i<p+header;++i) {
            const auto size=source[i];packet.insert(packet.end(),source.begin()+at,source.begin()+at+size);at+=size;
            if(size<255){packets.push_back(std::move(packet));packet.clear();}
        }
        p=at;
    }
    assert(packet.empty());return packets;
}
std::vector<std::pair<unsigned,std::vector<uint8_t>>> FlacBlocks(const std::vector<uint8_t>& source) {
    FlacInfo info;size_t audio=0;assert(ParseFlacMetadata(source,info,audio)==FlacParse::Valid);
    std::vector<std::pair<unsigned,std::vector<uint8_t>>> blocks;
    for(size_t p=4;p<audio;) {
        const size_t size=(size_t(source[p+1])<<16)|(size_t(source[p+2])<<8)|source[p+3];
        blocks.push_back({source[p]&127,std::vector<uint8_t>(source.begin()+p,source.begin()+p+4+size)});p+=4+size;
    }
    return blocks;
}
std::vector<uint8_t> AddBlock(const std::vector<uint8_t>& source,unsigned kind,const std::vector<uint8_t>& body) {
    FlacInfo info;size_t audio=0;assert(ParseFlacMetadata(source,info,audio)==FlacParse::Valid);
    auto result=source;size_t last=4;
    for(size_t p=4;p<audio;) {last=p;const size_t size=(size_t(source[p+1])<<16)|(size_t(source[p+2])<<8)|source[p+3];p+=4+size;}
    result[last]&=127;std::vector<uint8_t> block={uint8_t(kind|128),uint8_t(body.size()>>16),uint8_t(body.size()>>8),uint8_t(body.size())};
    block.insert(block.end(),body.begin(),body.end());result.insert(result.begin()+audio,block.begin(),block.end());return result;
}
std::vector<std::string> RawFields(const std::vector<uint8_t>& source,bool flac) {
    std::vector<uint8_t> comments;
    if(flac) {
        for(const auto& block:FlacBlocks(source))if(block.first==4)comments.assign(block.second.begin()+4,block.second.end());
    } else {const auto packets=Packets(source);comments.assign(packets[1].begin()+7,packets[1].end()-1);}
    size_t at=0;auto read=[&] {const auto size=Little(comments.data()+at);at+=4;std::string value(reinterpret_cast<const char*>(comments.data()+at),size);at+=size;return value;};
    read();const auto count=Little(comments.data()+at);at+=4;std::vector<std::string> fields;
    for(uint32_t i=0;i<count;++i)fields.push_back(read());
    assert(at==comments.size());return fields;
}
void Preserved(const std::vector<uint8_t>& source,const std::vector<uint8_t>& output,bool flac,bool replace_art=false) {
    if(flac) {
        FlacInfo info;size_t before=0,after=0;assert(ParseFlacMetadata(source,info,before)==FlacParse::Valid&&ParseFlacMetadata(output,info,after)==FlacParse::Valid);
        assert(std::vector<uint8_t>(source.begin()+before,source.end())==std::vector<uint8_t>(output.begin()+after,output.end()));
        const auto blocks=FlacBlocks(output);size_t next=0;
        for(const auto& old:FlacBlocks(source))if(old.first!=4 && !(replace_art && old.first==6)) {
            while(next<blocks.size() && blocks[next]!=old)++next;
            assert(next<blocks.size());++next; // Includes every original header, padding and picture byte.
        }
    } else {
        auto before=Packets(source),after=Packets(output);assert(before.size()==after.size());
        for(size_t i=0;i<before.size();++i)if(i!=1)assert(before[i]==after[i]);
    }
}
Tags Existing(bool flac) {
    Tags tags;tags.fields={{"TITLE","Track"},{"ARTIST","Artist"},{"ALBUM","Album"},{"TRACKNUMBER","1"},
        {"HISTORY_MEDIA_DURATION","1"},{"HISTORY_COMPLETE_LISTEN","1"},
        {"HISTORY_CAPTURE",flac?"native compressed FLAC input":"native compressed Ogg pages"},
        {"HISTORY_BITS_PER_SAMPLE","24"},{"HISTORY_NOMINAL_BITRATE","320000"},{"HISTORY_SAMPLES","16"},
        {"dAtE","2021-07-23"},{"GeNrE",""},{"cUsToM","first"},{"CUSTOM","second"},{"EMPTYCUSTOM",""},{"LYRICS","original plain lyrics"}};
    return tags;
}
std::vector<uint8_t> Source(const Tags& tags,bool flac) {
    std::vector<uint8_t> source;std::string error;
    assert(flac?TagFlac(recording_fixture::fixture(44100,24),tags,source,error):TagOgg(recording_fixture::ogg(),tags,source,error));
    if(flac){source=AddBlock(source,1,{0,0,11,22,0});source=AddBlock(source,2,{'T','E','S','T',9,8,7});}
    return source;
}
RecordingFile Inspect(const std::vector<uint8_t>& source) {
    RecordingFile record;assert(InspectRecording(source.data(),source.size(),record)&&record.complete&&record.owned);return record;
}
std::vector<uint8_t> SizedPng(unsigned width,unsigned height,uint8_t variant) {
    std::vector<uint8_t> raw;
    for(unsigned y=0;y<height;++y){raw.push_back(0);for(unsigned x=0;x<width;++x)raw.insert(raw.end(),{variant,uint8_t(x),uint8_t(y),255});}
    std::vector<uint8_t> compressed={0x78,0x01};size_t at=0;
    while(at<raw.size()) {
        const auto size=std::min(size_t(65535),raw.size()-at);compressed.push_back(uint8_t(at+size==raw.size()));
        compressed.push_back(uint8_t(size));compressed.push_back(uint8_t(size>>8));const auto inverse=uint16_t(~uint16_t(size));
        compressed.push_back(uint8_t(inverse));compressed.push_back(uint8_t(inverse>>8));
        compressed.insert(compressed.end(),raw.begin()+at,raw.begin()+at+size);at+=size;
    }
    uint32_t a=1,b=0;for(auto value:raw){a=(a+value)%65521;b=(b+a)%65521;}artwork_fixture::number(compressed,(b<<16)|a);
    std::vector<uint8_t> png={137,80,78,71,13,10,26,10},header;
    artwork_fixture::number(header,width);artwork_fixture::number(header,height);header.insert(header.end(),{8,6,0,0,0});
    artwork_fixture::chunk(png,"IHDR",header);artwork_fixture::chunk(png,"IDAT",compressed);artwork_fixture::chunk(png,"IEND",{});return png;
}
std::string Base64(const std::vector<uint8_t>& bytes) {
    const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string text;
    for(size_t i=0;i<bytes.size();i+=3) {
        const uint32_t n=(uint32_t(bytes[i])<<16)|(i+1<bytes.size()?uint32_t(bytes[i+1])<<8:0)|(i+2<bytes.size()?bytes[i+2]:0);
        text+=alphabet[n>>18];text+=alphabet[(n>>12)&63];text+=i+1<bytes.size()?alphabet[(n>>6)&63]:'=';text+=i+2<bytes.size()?alphabet[n&63]:'=';
    }
    return text;
}
std::vector<uint8_t> PicturePayload(const std::string& mime,const std::vector<uint8_t>& image) {
    std::vector<uint8_t> payload;artwork_fixture::number(payload,3);artwork_fixture::number(payload,uint32_t(mime.size()));
    payload.insert(payload.end(),mime.begin(),mime.end());artwork_fixture::number(payload,0);
    for(unsigned value:{1024u,1024u,32u,0u})artwork_fixture::number(payload,value);
    artwork_fixture::number(payload,uint32_t(image.size()));payload.insert(payload.end(),image.begin(),image.end());return payload;
}
void ArtworkUpgradeCase(bool flac) {
    Catalog catalog;catalog.title=L"Track";catalog.artist=L"Artist";catalog.album=L"Album";catalog.track=1;catalog.duration=1;
    auto old=Existing(flac);Set(old,"YEAR","2021");old.cover=SizedPng(8,8,1);old.mime="image/png";
    const auto source=Source(old,flac);const auto inspected=Inspect(source);
    assert(inspected.artwork.valid && inspected.artwork.width==8 && inspected.artwork.height==8 && inspected.artworks.size()==1);
    RecordingArtwork fingerprint;assert(ReadRecordingArtwork(old.cover,fingerprint) && fingerprint.digest==inspected.artwork.digest && fingerprint.bytes==inspected.artwork.bytes);
    Tags original;original.cover=SizedPng(16,16,2);original.mime="image/png";original.fields={{"FLOGGFY_ARTWORK_SOURCE","spotify-client"}};
    std::vector<uint8_t> updated,second;std::string error;
    Tags marker;marker.fields=original.fields;
    assert(!NeedsRecordingMetadata(inspected,marker));
    assert(FillRecordingMetadata(source,marker,catalog,updated,error)==MetadataUpdate::Unchanged);
    assert(NeedsRecordingMetadata(inspected,original));
    assert(FillRecordingMetadata(source,original,catalog,updated,error)==MetadataUpdate::Updated);
    auto upgraded=Inspect(updated);assert(upgraded.artwork.width==16 && upgraded.artwork.height==16 && Value(upgraded.tags,"FLOGGFY_ARTWORK_SOURCE")=="spotify-client");
    assert(ReadRecordingArtwork(original.cover,fingerprint) && fingerprint.digest==upgraded.artwork.digest);Preserved(source,updated,flac,true);
    assert(!NeedsRecordingMetadata(upgraded,original) && FillRecordingMetadata(updated,original,catalog,second,error)==MetadataUpdate::Unchanged);

    // Identical encoded image bytes are already good, even without provenance.
    auto identical=original;identical.cover=old.cover;
    assert(!NeedsRecordingMetadata(inspected,identical) && FillRecordingMetadata(source,identical,catalog,second,error)==MetadataUpdate::Unchanged);
    assert(Value(inspected.tags,"FLOGGFY_ARTWORK_SOURCE").empty());

    // Trusted equal-sized original art replaces an unmarked thumbnail once.
    auto same_size=original;same_size.cover=SizedPng(8,8,3);
    assert(NeedsRecordingMetadata(inspected,same_size) && FillRecordingMetadata(source,same_size,catalog,second,error)==MetadataUpdate::Updated);
    auto trusted=Inspect(second);assert(trusted.artwork.width==8 && Value(trusted.tags,"FLOGGFY_ARTWORK_SOURCE")=="spotify-client");
    same_size.cover=SizedPng(8,8,4);
    assert(!NeedsRecordingMetadata(trusted,same_size) && FillRecordingMetadata(second,same_size,catalog,updated,error)==MetadataUpdate::Unchanged);
    auto untrusted_equal=same_size;untrusted_equal.fields.clear();
    assert(!NeedsRecordingMetadata(inspected,untrusted_equal));

    // Resolution is component-wise: neither a lower pixel count nor a narrow
    // aspect-ratio change can throw away a known better dimension.
    auto small=original;small.cover=SizedPng(4,4,2);assert(!NeedsRecordingMetadata(inspected,small));
    small.cover=SizedPng(16,4,2);assert(!NeedsRecordingMetadata(inspected,small));
    auto bigger=old;bigger.cover=SizedPng(32,32,1);assert(!NeedsRecordingMetadata(Inspect(Source(bigger,flac)),original));
    auto fallback=original;fallback.fields.clear();
    assert(!NeedsRecordingMetadata(trusted,fallback));
    assert(FillRecordingMetadata(second,fallback,catalog,updated,error)==MetadataUpdate::Unchanged);
    auto missing=old;missing.cover.clear();missing.mime.clear();
    assert(FillRecordingMetadata(Source(missing,flac),fallback,catalog,updated,error)==MetadataUpdate::Updated);
    assert(Inspect(updated).artwork.width==16 && Value(Inspect(updated).tags,"FLOGGFY_ARTWORK_SOURCE").empty());
    assert(!NeedsRecordingMetadata(Inspect(updated),fallback));

    // Complete front-cover comparison also handles a last PICTURE block and
    // multiple comparable fronts, without modifying non-cover metadata.
    if(flac) {
        std::vector<uint8_t> last_picture;assert(TagFlac(recording_fixture::fixture(44100,24),old,last_picture,error));
        assert(FillRecordingMetadata(last_picture,original,catalog,updated,error)==MetadataUpdate::Updated);
        assert(Inspect(updated).artwork.width==16);Preserved(last_picture,updated,true,true);
    }
    Tags medium;medium.cover=SizedPng(16,16,7);medium.mime="image/png";std::vector<uint8_t> medium_picture;assert(BuildPicture(medium,medium_picture));
    std::vector<uint8_t> multiple;
    if(flac)multiple=AddBlock(source,6,medium_picture);
    else {auto encoded=old;encoded.fields.emplace_back("METADATA_BLOCK_PICTURE",Base64(medium_picture));multiple=Source(encoded,false);}
    assert(Inspect(multiple).artworks.size()==2 && Inspect(multiple).artwork.width==16);
    assert(!NeedsRecordingMetadata(Inspect(multiple),identical));
    auto largest=original;largest.cover=SizedPng(32,32,8);
    assert(FillRecordingMetadata(multiple,largest,catalog,updated,error)==MetadataUpdate::Updated);
    assert(Inspect(updated).artworks.size()==1 && Inspect(updated).artwork.width==32);Preserved(multiple,updated,flac,true);
    auto invalid_candidate=original;invalid_candidate.cover={1,2,3};assert(!NeedsRecordingMetadata(inspected,invalid_candidate));

    // Wrapper dimensions are untrusted; compare the actual PNG bytes/dimensions.
    Tags picture_tags;picture_tags.cover=old.cover;picture_tags.mime="image/png";std::vector<uint8_t> picture;
    assert(BuildPicture(picture_tags,picture));const size_t width=4+4+picture_tags.mime.size()+4;
    picture[width]=0;picture[width+1]=0;picture[width+2]=16;picture[width+3]=0;
    auto no_art=old;no_art.cover.clear();no_art.mime.clear();std::vector<uint8_t> wrapped;
    if(flac)wrapped=AddBlock(Source(no_art,flac),6,picture);
    else {auto encoded=no_art;encoded.fields.emplace_back("METADATA_BLOCK_PICTURE",Base64(picture));wrapped=Source(encoded,false);}
    assert(Inspect(wrapped).artwork.width==8 && !NeedsRecordingMetadata(Inspect(wrapped),identical));

    // Invalid known front-cover payloads can be replaced. Other/ambiguous
    // picture layouts are preserved conservatively, including back/booklet art.
    const std::vector<uint8_t> invalid_front={0,0,0,3,1,2,3};std::vector<uint8_t> invalid;
    if(flac)invalid=AddBlock(Source(no_art,true),6,invalid_front);
    else {auto encoded=no_art;encoded.fields.emplace_back("METADATA_BLOCK_PICTURE",Base64(invalid_front));invalid=Source(encoded,false);}
    assert(Inspect(invalid).has_artwork && !Inspect(invalid).artwork.valid && Inspect(invalid).artwork_edit_safe);
    assert(NeedsRecordingMetadata(Inspect(invalid),original) && FillRecordingMetadata(invalid,original,catalog,updated,error)==MetadataUpdate::Updated);
    assert(Inspect(updated).artwork.width==16);Preserved(invalid,updated,flac,true);
    const auto large_image=SizedPng(1024,1024,4);assert(large_image.size()>4*1024*1024);
    const std::vector<uint8_t> gif={'G','I','F','8','9','a',1,0,1,0,128,0,0,0,0,0,255,255,255,33,249,4,1,0,0,0,0,44,0,0,0,0,1,0,1,0,0,2,2,68,1,0,59};
    for(const auto& opaque_picture:{PicturePayload("image/png",large_image),PicturePayload("image/gif",gif)}) {
        std::vector<uint8_t> opaque;
        if(flac)opaque=AddBlock(Source(no_art,true),6,opaque_picture);
        else {auto encoded=no_art;encoded.fields.emplace_back("METADATA_BLOCK_PICTURE",Base64(opaque_picture));opaque=Source(encoded,false);}
        const auto opaque_record=Inspect(opaque);assert(opaque_record.has_artwork && !opaque_record.artwork.valid && !opaque_record.artwork_edit_safe);
        assert(!NeedsRecordingMetadata(opaque_record,original) && FillRecordingMetadata(opaque,original,catalog,updated,error)==MetadataUpdate::Unchanged);
        auto text=original;text.fields.emplace_back("GENRE","Rock");
        assert(FillRecordingMetadata(opaque,text,catalog,updated,error)==MetadataUpdate::Updated);Preserved(opaque,updated,flac);
    }
    picture[0]=0;picture[1]=0;picture[2]=0;picture[3]=4;std::vector<uint8_t> booklet;
    if(flac)booklet=AddBlock(source,6,picture);
    else {auto encoded=old;encoded.fields.emplace_back("METADATA_BLOCK_PICTURE",Base64(picture));booklet=Source(encoded,false);}
    assert(!Inspect(booklet).artwork_edit_safe && !NeedsRecordingMetadata(Inspect(booklet),original));
    assert(FillRecordingMetadata(booklet,original,catalog,updated,error)==MetadataUpdate::Unchanged);
    auto text_and_art=original;text_and_art.fields.emplace_back("GENRE","Rock");
    assert(FillRecordingMetadata(booklet,text_and_art,catalog,updated,error)==MetadataUpdate::Updated);
    assert(Value(Inspect(updated).tags,"FLOGGFY_ARTWORK_SOURCE").empty());Preserved(booklet,updated,flac);
}
void CodecCases(bool flac) {
    Catalog catalog;catalog.title=L"Track";catalog.artist=L"Artist";catalog.album=L"Album";catalog.track=1;catalog.duration=1;catalog.spotify_uri="spotify:track:metadata";
    auto existing=Existing(flac);const auto source=Source(existing,flac);assert(!Inspect(source).has_artwork);
    Tags incoming;incoming.fields={{"GENRE","Rock"},{"YEAR","1999"},{"DATE","2024-01-02"},
        {"SPOTIFY_URI",catalog.spotify_uri},{"CUSTOM","replacement"},{"CUSTOM","another replacement"},{"emptycustom","filled"},
        {"TITLE","different title"},{"HISTORY_BITS_PER_SAMPLE","16"},{"history_samples","8"},{"HISTORY_NEW_CAPTURE_FACT","bad"},
        {"LYRICS","[00:01.00] synchronized\n[00:02.000] second line"}};
    incoming.cover=artwork_fixture::png();incoming.mime="image/png";
    std::vector<uint8_t> output,second;std::string error;
    assert(NeedsRecordingMetadata(Inspect(source),incoming));
    assert(FillRecordingMetadata(source,incoming,catalog,output,error)==MetadataUpdate::Updated && error.empty());
    const auto record=Inspect(output);Preserved(source,output,flac);
    assert(Value(record.tags,"GENRE")=="Rock" && Value(record.tags,"YEAR")=="2021" && Value(record.tags,"DATE")=="2021-07-23");
    assert(Value(record.tags,"TITLE")=="Track" && Value(record.tags,"SPOTIFY_URI")==catalog.spotify_uri && record.has_artwork);
    assert(Value(record.tags,"HISTORY_BITS_PER_SAMPLE")=="24" && Value(record.tags,"HISTORY_SAMPLES")=="16" && Value(record.tags,"HISTORY_NEW_CAPTURE_FACT").empty());
    assert((Values(record.tags,"CUSTOM")==std::vector<std::string>{"first","second"}) && Value(record.tags,"EMPTYCUSTOM")=="filled");
    assert(IsSyncedLyrics(Value(record.tags,"LYRICS")));
    const auto fields=RawFields(output,flac);
    assert(std::find(fields.begin(),fields.end(),"cUsToM=first")!=fields.end() && std::find(fields.begin(),fields.end(),"CUSTOM=second")!=fields.end());
    assert(!NeedsRecordingMetadata(record,incoming));
    assert(FillRecordingMetadata(output,incoming,catalog,second,error)==MetadataUpdate::Unchanged && second.empty() && error.empty());
    auto wrong=catalog;wrong.spotify_uri="spotify:track:other";
    assert(FillRecordingMetadata(output,incoming,wrong,second,error)==MetadataUpdate::Invalid && second.empty());
    auto partial=source;partial.pop_back();assert(FillRecordingMetadata(partial,incoming,catalog,second,error)==MetadataUpdate::Invalid && second.empty());
    auto unowned=existing;Set(unowned,"HISTORY_COMPLETE_LISTEN","0");
    assert(FillRecordingMetadata(Source(unowned,flac),incoming,catalog,second,error)==MetadataUpdate::Invalid && second.empty());

    // Existing art and legacy picture comments are opaque data, never replacements.
    auto pictured=existing;Set(pictured,"YEAR","2021");pictured.cover=artwork_fixture::png();pictured.mime="image/png";
    auto pictured_source=Source(pictured,flac);Tags other_picture;other_picture.cover=artwork_fixture::png(true);other_picture.mime="image/png";
    assert(Inspect(pictured_source).has_artwork);
    assert(FillRecordingMetadata(pictured_source,other_picture,catalog,second,error)==MetadataUpdate::Unchanged && second.empty());
    other_picture.fields={{"COMMENT","new note"}};
    assert(FillRecordingMetadata(pictured_source,other_picture,catalog,second,error)==MetadataUpdate::Updated);Preserved(pictured_source,second,flac);
    auto legacy=existing;Set(legacy,"YEAR","2021");legacy.fields.emplace_back("METADATA_BLOCK_PICTURE","opaque legacy picture");
    const auto legacy_source=Source(legacy,flac);assert(Inspect(legacy_source).has_artwork);
    assert(FillRecordingMetadata(legacy_source,other_picture,catalog,second,error)==MetadataUpdate::Updated);Preserved(legacy_source,second,flac);
    const auto legacy_fields=RawFields(second,flac);assert(std::find(legacy_fields.begin(),legacy_fields.end(),"METADATA_BLOCK_PICTURE=opaque legacy picture")!=legacy_fields.end());
    if(flac){const auto opaque=AddBlock(Source(existing,flac),6,{1,2,3,4});assert(Inspect(opaque).has_artwork);assert(FillRecordingMetadata(opaque,incoming,catalog,second,error)==MetadataUpdate::Updated);Preserved(opaque,second,flac);}

    // Invalid artwork never prevents safe text fills and never adds a picture.
    Tags invalid_art;invalid_art.cover={1,2,3};invalid_art.mime="image/png";invalid_art.fields={{"GENRE","Jazz"}};
    assert(FillRecordingMetadata(source,invalid_art,catalog,second,error)==MetadataUpdate::Updated && !Inspect(second).has_artwork);
    Preserved(source,second,flac);

    // Both aliases preserve existing words; only a real synchronized upgrade adds LYRICS.
    auto alias=existing;Set(alias,"YEAR","2021");Set(alias,"LYRICS","");Set(alias,"UNSYNCEDLYRICS","original alias");
    const auto alias_source=Source(alias,flac);Tags plain;plain.fields={{"LYRICS","replacement plain"}};
    assert(FillRecordingMetadata(alias_source,plain,catalog,second,error)==MetadataUpdate::Unchanged);
    Tags synced;synced.fields={{"LYRICS","[00:01.00] synchronized"}};
    assert(FillRecordingMetadata(alias_source,synced,catalog,second,error)==MetadataUpdate::Updated);
    assert(Value(Inspect(second).tags,"UNSYNCEDLYRICS")=="original alias" && IsSyncedLyrics(Value(Inspect(second).tags,"LYRICS")));Preserved(alias_source,second,flac);
    Set(alias,"UNSYNCEDLYRICS","[00:01.00] existing synchronized alias");
    assert(FillRecordingMetadata(Source(alias,flac),synced,catalog,second,error)==MetadataUpdate::Unchanged);
    auto synced_existing=existing;Set(synced_existing,"YEAR","2021");Set(synced_existing,"LYRICS","[00:01.00] existing synchronized words");
    assert(FillRecordingMetadata(Source(synced_existing,flac),plain,catalog,second,error)==MetadataUpdate::Unchanged);
    assert(FillRecordingMetadata(Source(synced_existing,flac),synced,catalog,second,error)==MetadataUpdate::Unchanged);

    auto whitespace=existing;Set(whitespace,"YEAR"," \t ");Set(whitespace,"GENRE"," \r\n ");Set(whitespace,"LYRICS"," \t ");
    Tags nonblank;nonblank.fields={{"GENRE","Rock"},{"LYRICS","new plain lyrics"}};
    assert(FillRecordingMetadata(Source(whitespace,flac),nonblank,catalog,second,error)==MetadataUpdate::Updated);
    assert(Value(Inspect(second).tags,"GENRE")=="Rock" && Value(Inspect(second).tags,"YEAR")=="2021" && Value(Inspect(second).tags,"LYRICS")=="new plain lyrics");
    Set(whitespace,"YEAR","2021");Tags blanks;blanks.fields={{"GENRE"," \r\n "},{"LYRICS"," \t "},{"COMMENT","   "}};
    assert(FillRecordingMetadata(Source(whitespace,flac),blanks,catalog,second,error)==MetadataUpdate::Unchanged);

    // A populated YEAR rejects a conflicting incoming DATE. Existing DATE wins derivation.
    auto year=existing;Set(year,"DATE","");Set(year,"YEAR","2020");Tags conflict;conflict.fields={{"DATE","2021-01-01"},{"YEAR","2021"}};
    assert(FillRecordingMetadata(Source(year,flac),conflict,catalog,second,error)==MetadataUpdate::Unchanged);
    conflict.fields={{"DATE","2020-03-04"}};
    assert(FillRecordingMetadata(Source(year,flac),conflict,catalog,second,error)==MetadataUpdate::Updated && Value(Inspect(second).tags,"DATE")=="2020-03-04");
    Set(year,"YEAR","");conflict.fields={{"DATE","2021-01-01"},{"YEAR","1999"}};
    assert(FillRecordingMetadata(Source(year,flac),conflict,catalog,second,error)==MetadataUpdate::Updated && Value(Inspect(second).tags,"YEAR")=="2021");
}
}
int main() {
    CodecCases(true);CodecCases(false);ArtworkUpgradeCase(true);ArtworkUpgradeCase(false);
    std::puts("PASS: owned complete FLAC/Ogg metadata fills preserve populated/custom/technical fields and all compressed audio; missing dates, genres, URI, lyrics and artwork fill safely");
    std::puts("PASS: shared Needs planner avoids marker-only/identical/repeated writes; validated front-cover resolution and trusted provenance upgrades never downgrade or remove booklets");
    std::puts("PASS: FLAC opaque blocks/padding/art and Ogg packets survive; synchronized lyrics/aliases and populated dates/years never downgrade; second pass is unchanged; partial/unowned/wrong recordings reject");
}
