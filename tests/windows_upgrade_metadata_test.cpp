#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../native/file_publication.h"
#include "../native/flac_history_core.h"
#include "../native/metadata_update_core.h"
#include "recording_fixture.h"
#include "artwork_fixture.h"
#include <cassert>
#include <cstdio>
using namespace history;
namespace history { std::wstring WindowsPath(const std::wstring& path) {return path;} }
namespace {
std::string Value(const Tags& tags,const char* key) {
    for(const auto& field:tags.fields)if(field.first==key)return field.second;
    return {};
}
void Write(const std::wstring& path,const std::vector<uint8_t>& bytes) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
    assert(file!=INVALID_HANDLE_VALUE);
    DWORD written=0;
    assert(WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr) && written==bytes.size());
    CloseHandle(file);
}
std::vector<uint8_t> Read(const std::wstring& path) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    assert(file!=INVALID_HANDLE_VALUE);
    const DWORD size=GetFileSize(file,nullptr);
    assert(size!=INVALID_FILE_SIZE);
    std::vector<uint8_t> bytes(size);
    DWORD read=0;
    assert(ReadFile(file,bytes.data(),size,&read,nullptr) && read==size);
    CloseHandle(file);
    return bytes;
}
std::vector<uint8_t> Audio(const std::vector<uint8_t>& bytes) {
    FlacInfo info;size_t offset=0;
    assert(ParseFlacMetadata(bytes,info,offset)==FlacParse::Valid);
    return {bytes.begin()+offset,bytes.end()};
}
std::vector<std::vector<uint8_t>> Packets(const std::vector<uint8_t>& bytes) {
    std::vector<std::vector<uint8_t>> packets;
    std::vector<uint8_t> packet;
    for(size_t page=0;page<bytes.size();) {
        const size_t header=27+bytes[page+26];
        size_t body=page+header;
        for(size_t at=page+27;at<page+header;++at) {
            const size_t length=bytes[at];
            packet.insert(packet.end(),bytes.begin()+body,bytes.begin()+body+length);
            body+=length;
            if(length<255){packets.push_back(std::move(packet));packet.clear();}
        }
        page=body;
    }
    assert(packet.empty());
    return packets;
}
std::vector<uint8_t> Tagged(unsigned bits,const Tags& tags) {
    std::vector<uint8_t> bytes;std::string error;
    assert(TagFlac(recording_fixture::fixture(44100,bits),tags,bytes,error));
    return bytes;
}
bool check_lock=false,write_denied=false;
}
namespace history {
void PublicationTestPhase(const wchar_t* phase,const std::wstring& path) {
    if(!check_lock || std::wstring(phase)!=L"staged")return;
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    write_denied=file==INVALID_HANDLE_VALUE && GetLastError()==ERROR_SHARING_VIOLATION;
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
}
}
int main() {
    wchar_t temp[MAX_PATH];assert(GetTempPathW(MAX_PATH,temp));
    const auto root=std::wstring(temp)+L"Floggfy-upgrade-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    assert(CreateDirectoryW(root.c_str(),nullptr));
    const auto path=root+L"\\recording.flac";
    Catalog catalog;catalog.spotify_uri="spotify:track:upgrade";
    Tags old;
    old.fields={{"TITLE","Saved title"},{"ARTIST","Artist"},{"ALBUM","Album"},{"SPOTIFY_URI",catalog.spotify_uri},
        {"LYRICS","[00:00.000]Saved synced words"},{"COMMENT","user note"},{"CUSTOM","one"},{"CUSTOM","two"},
        {"YEAR","1999"},{"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_CAPTURE","native compressed FLAC input"},{"HISTORY_BITS_PER_SAMPLE","16"}};
    old.cover=artwork_fixture::jpeg();old.mime="image/jpeg";
    Tags incoming;
    incoming.fields={{"TITLE","Client title"},{"ARTIST","Artist"},{"ALBUM","Album"},{"SPOTIFY_URI",catalog.spotify_uri},
        {"LYRICS","plain incoming words"},{"DATE","2001-02-03"},{"GENRE","Rock"},
        {"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_CAPTURE","native compressed FLAC input"},{"HISTORY_BITS_PER_SAMPLE","24"}};
    const Quality better{Codec::Flac,44100,2,24,0};
    // Missing, invalid and smaller fallback artwork must all preserve the old
    // cover, user fields, repeats and synced lyrics during real publication.
    for(unsigned scenario=0;scenario<3;++scenario) {
        Tags next=incoming;
        if(scenario==1){next.cover={1,2,3};next.mime="image/jpeg";}
        if(scenario==2){next.cover=artwork_fixture::png();next.mime="image/png";next.fields.emplace_back("FLOGGFY_ARTWORK_SOURCE","spotify-client");}
        const auto original=Tagged(16,old),replacement=Tagged(24,next);
        Write(path,original);check_lock=true;write_denied=false;size_t saved=0;
        assert(PublishBytes(path,better,replacement.data(),replacement.size(),catalog,&saved)==FilePublication::Upgraded);
        check_lock=false;assert(write_denied);
        const auto actual=Read(path);assert(saved==actual.size() && Audio(actual)==Audio(replacement));
        RecordingFile inspected;
        assert(InspectRecording(actual.data(),actual.size(),inspected) && inspected.complete && MatchesRecording(inspected,catalog));
        assert(inspected.quality.bits==24 && inspected.tags.cover==old.cover);
        assert(Value(inspected.tags,"TITLE")=="Saved title" && Value(inspected.tags,"COMMENT")=="user note");
        assert(Value(inspected.tags,"LYRICS")==Value(old,"LYRICS") && Value(inspected.tags,"YEAR")=="1999");
        assert(Value(inspected.tags,"DATE").empty() && Value(inspected.tags,"GENRE")=="Rock");
        assert(Value(inspected.tags,"FLOGGFY_ARTWORK_SOURCE").empty());
        assert(Value(inspected.tags,"HISTORY_BITS_PER_SAMPLE")=="24");
        unsigned custom=0;for(const auto& field:inspected.tags.fields)if(field.first=="CUSTOM")++custom;
        assert(custom==2 && GetFileAttributesW((path+L".previous").c_str())==INVALID_FILE_ATTRIBUTES);
    }
    // The shared comparison policy selects a larger trusted client cover.
    auto small=old;small.cover=artwork_fixture::png();small.mime="image/png";
    auto larger=incoming;larger.cover=artwork_fixture::jpeg();larger.mime="image/jpeg";
    larger.fields.emplace_back("FLOGGFY_ARTWORK_SOURCE","spotify-client");
    Write(path,Tagged(16,small));const auto replacement=Tagged(24,larger);
    assert(PublishBytes(path,better,replacement.data(),replacement.size(),catalog)==FilePublication::Upgraded);
    RecordingFile inspected;assert(ReadRecording(path,inspected) && inspected.tags.cover==larger.cover);
    assert(Value(inspected.tags,"FLOGGFY_ARTWORK_SOURCE")=="spotify-client");
    // No trust marker: a larger untrusted image is not proof of better art.
    larger.fields.pop_back();Write(path,Tagged(16,small));const auto fallback=Tagged(24,larger);
    assert(PublishBytes(path,better,fallback.data(),fallback.size(),catalog)==FilePublication::Upgraded);
    assert(ReadRecording(path,inspected) && inspected.tags.cover==small.cover);
    // A synced alias must not be shadowed by rejected plain incoming LYRICS.
    auto alias=old;
    for(auto& field:alias.fields)if(field.first=="LYRICS")field.first="UNSYNCEDLYRICS";
    Write(path,Tagged(16,alias));const auto plain=Tagged(24,incoming);
    assert(PublishBytes(path,better,plain.data(),plain.size(),catalog)==FilePublication::Upgraded);
    assert(ReadRecording(path,inspected) && Value(inspected.tags,"LYRICS").empty());
    assert(Value(inspected.tags,"UNSYNCEDLYRICS")==Value(old,"LYRICS"));
    // The complete-comment replacement policy applies to Ogg upgrades as well;
    // the shared helper preserves packet bytes while merging metadata.
    std::vector<uint8_t> old_ogg,new_ogg,merged_ogg;std::string error;
    // Fixtures use Ogg ownership markers, not the FLAC marker above.
    auto old_vorbis=old,next_vorbis=incoming;
    for(auto* tags:{&old_vorbis,&next_vorbis})for(auto& field:tags->fields)
        if(field.first=="HISTORY_CAPTURE")field.second="native compressed Ogg pages";
    assert(TagOgg(recording_fixture::ogg(),old_vorbis,old_ogg,error));
    assert(TagOgg(recording_fixture::ogg(),next_vorbis,new_ogg,error));
    assert(PreserveUpgradeMetadata(old_ogg,new_ogg,catalog,merged_ogg,error));
    assert(InspectRecording(merged_ogg.data(),merged_ogg.size(),inspected) && inspected.complete);
    assert(inspected.tags.cover==old.cover && Value(inspected.tags,"LYRICS")==Value(old,"LYRICS"));
    assert(Value(inspected.tags,"DATE").empty() && Value(inspected.tags,"COMMENT")=="user note");
    const auto before_packets=Packets(new_ogg),after_packets=Packets(merged_ogg);
    assert(before_packets.size()==after_packets.size());
    for(size_t i=0;i<before_packets.size();++i)if(i!=1)assert(before_packets[i]==after_packets[i]);
    // Identity mismatch cannot borrow or overwrite another recording's metadata.
    auto wrong=catalog;wrong.spotify_uri="spotify:track:other";
    const auto retained=Read(path);
    assert(PublishBytes(path,better,replacement.data(),replacement.size(),wrong)==FilePublication::Failed && Read(path)==retained);
    // Publication remains protected by sharing constraints.
    Write(path,Tagged(16,old));HANDLE held=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    assert(held!=INVALID_HANDLE_VALUE);
    assert(PublishBytes(path,better,replacement.data(),replacement.size(),catalog)==FilePublication::Failed);
    CloseHandle(held);assert(ReadRecording(path,inspected) && inspected.quality.bits==16 && inspected.tags.cover==old.cover);
    assert(DeleteFileW(path.c_str()) && RemoveDirectoryW(root.c_str()));
    puts("PASS: locked audio upgrades retain existing artwork, synchronized lyrics and custom metadata; only better trusted art replaces covers; incoming audio and identity stay intact");
}
