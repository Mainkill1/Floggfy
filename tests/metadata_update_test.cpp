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
void Preserved(const std::vector<uint8_t>& source,const std::vector<uint8_t>& output,bool flac) {
    if(flac) {
        FlacInfo info;size_t before=0,after=0;assert(ParseFlacMetadata(source,info,before)==FlacParse::Valid&&ParseFlacMetadata(output,info,after)==FlacParse::Valid);
        assert(std::vector<uint8_t>(source.begin()+before,source.end())==std::vector<uint8_t>(output.begin()+after,output.end()));
        const auto blocks=FlacBlocks(output);size_t next=0;
        for(const auto& old:FlacBlocks(source))if(old.first!=4) {
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
void CodecCases(bool flac) {
    Catalog catalog;catalog.title=L"Track";catalog.artist=L"Artist";catalog.album=L"Album";catalog.track=1;catalog.duration=1;catalog.spotify_uri="spotify:track:metadata";
    auto existing=Existing(flac);const auto source=Source(existing,flac);assert(!Inspect(source).has_artwork);
    Tags incoming;incoming.fields={{"GENRE","Rock"},{"YEAR","1999"},{"DATE","2024-01-02"},
        {"SPOTIFY_URI",catalog.spotify_uri},{"CUSTOM","replacement"},{"CUSTOM","another replacement"},{"emptycustom","filled"},
        {"TITLE","different title"},{"HISTORY_BITS_PER_SAMPLE","16"},{"history_samples","8"},{"HISTORY_NEW_CAPTURE_FACT","bad"},
        {"LYRICS","[00:01.00] synchronized\n[00:02.000] second line"}};
    incoming.cover=artwork_fixture::png();incoming.mime="image/png";
    std::vector<uint8_t> output,second;std::string error;
    assert(FillRecordingMetadata(source,incoming,catalog,output,error)==MetadataUpdate::Updated && error.empty());
    const auto record=Inspect(output);Preserved(source,output,flac);
    assert(Value(record.tags,"GENRE")=="Rock" && Value(record.tags,"YEAR")=="2021" && Value(record.tags,"DATE")=="2021-07-23");
    assert(Value(record.tags,"TITLE")=="Track" && Value(record.tags,"SPOTIFY_URI")==catalog.spotify_uri && record.has_artwork);
    assert(Value(record.tags,"HISTORY_BITS_PER_SAMPLE")=="24" && Value(record.tags,"HISTORY_SAMPLES")=="16" && Value(record.tags,"HISTORY_NEW_CAPTURE_FACT").empty());
    assert((Values(record.tags,"CUSTOM")==std::vector<std::string>{"first","second"}) && Value(record.tags,"EMPTYCUSTOM")=="filled");
    assert(IsSyncedLyrics(Value(record.tags,"LYRICS")));
    const auto fields=RawFields(output,flac);
    assert(std::find(fields.begin(),fields.end(),"cUsToM=first")!=fields.end() && std::find(fields.begin(),fields.end(),"CUSTOM=second")!=fields.end());
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
    CodecCases(true);CodecCases(false);
    std::puts("PASS: owned complete FLAC/Ogg metadata fills preserve populated/custom/technical fields and all compressed audio; missing dates, genres, URI, lyrics and artwork fill safely");
    std::puts("PASS: FLAC opaque blocks/padding/art and Ogg packets survive; synchronized lyrics/aliases and populated dates/years never downgrade; second pass is unchanged; partial/unowned/wrong recordings reject");
}
