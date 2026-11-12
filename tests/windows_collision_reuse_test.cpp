#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../native/file_publication.h"
#include "../native/flac_history_core.h"
#include "recording_fixture.h"
#include <cassert>
#include <cstdio>
using namespace history;
namespace history { std::wstring WindowsPath(const std::wstring& path) {return path;} }
static void Write(const std::wstring& path,const std::vector<uint8_t>& bytes) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);assert(file!=INVALID_HANDLE_VALUE);
    DWORD written=0;assert(WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr) && written==bytes.size());CloseHandle(file);
}
static std::vector<uint8_t> Tagged(const Catalog& catalog,unsigned bits) {
    const std::string title=catalog.title.empty()?"Collision":std::string(catalog.title.begin(),catalog.title.end());
    Tags tags;tags.fields={{"TITLE",title},{"ARTIST","Artist"},{"ALBUM","Album"},{"SPOTIFY_URI",catalog.spotify_uri},
        {"HISTORY_MEDIA_DURATION",std::to_string(catalog.duration)},
        {"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_CAPTURE","native compressed FLAC input"}};
    std::vector<uint8_t> bytes;std::string error;
    assert(TagFlac(recording_fixture::fixture(44100,bits),tags,bytes,error));return bytes;
}
int main() {
    wchar_t temp[MAX_PATH];assert(GetTempPathW(MAX_PATH,temp));
    const auto root=std::wstring(temp)+L"Floggfy-collision-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    assert(CreateDirectoryW(root.c_str(),nullptr));const auto base=root+L"\\01 - Intro.flac";
    Catalog a,b;a.spotify_uri="spotify:track:AAAAAAAAAAAAAAAAAAAAAA";b.spotify_uri="spotify:track:BBBBBBBBBBBBBBBBBBBBBB";
    Write(base,Tagged(a,16));const auto first_conflict=ResolveRecordingDestination(base,b);
    assert(first_conflict.state==RecordingDestinationState::Vacant&&first_conflict.explicit_uid_conflict);
    const auto suffix=first_conflict.path;
    assert(!suffix.empty() && suffix!=base);Write(suffix,Tagged(b,24));
    const auto exact_after_conflict=ResolveRecordingDestination(base,b);
    assert(exact_after_conflict.state==RecordingDestinationState::Existing&&exact_after_conflict.path==suffix&&
           exact_after_conflict.explicit_uid_conflict);
    const auto same_identity=ResolveRecordingDestination(base,a);
    assert(same_identity.state==RecordingDestinationState::Existing&&same_identity.path==base&&
           !same_identity.explicit_uid_conflict);
    assert(DeleteFileW(base.c_str()));
    const Quality lower{Codec::Flac,44100,2,16,0},better{Codec::Flac,44100,2,32,0};
    // No resolver state is retained: repeated fresh calls and a vacant ordinary
    // filename must still locate the same higher-quality collision recording.
    assert(ResolveRecordingDestination(base,b).path==suffix && ResolveRecordingDestination(base,b).path==suffix);
    assert(DecideSave(suffix,lower,b)==SaveDecision::Skip && GetFileAttributesW(base.c_str())==INVALID_FILE_ATTRIBUTES);
    const auto high=Tagged(b,32);
    assert(PublishBytes(ResolveRecordingDestination(base,b).path,better,high.data(),high.size(),b)==FilePublication::Upgraded);
    Quality actual;assert(ExistingRecording(suffix,b,actual) && actual.bits==32);
    // A preexisting lower-quality duplicate at the ordinary name must not hide
    // the higher-quality suffixed recording either.
    Write(base,Tagged(b,16));assert(ResolveRecordingDestination(base,b).path==suffix);assert(DeleteFileW(base.c_str()));
    // Inaccessible possible matches cause retry, never a new duplicate.
    HANDLE held=CreateFileW(suffix.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);assert(held!=INVALID_HANDLE_VALUE);
    assert(ResolveRecordingDestination(base,b).path.empty());CloseHandle(held);
    // A partial candidate is repairable only when no complete matching file exists.
    auto partial=Tagged(b,16);partial.pop_back();Write(base,partial);assert(ResolveRecordingDestination(base,b).path==suffix);
    assert(DeleteFileW(suffix.c_str()));assert(ResolveRecordingDestination(base,b).path==base);assert(DeleteFileW(base.c_str()));
    // Unrelated/corrupted occupants are preserved and bounded names disambiguate.
    Write(base,Tagged(a,16));Write(suffix,{1,2,3});const auto alternate=ResolveRecordingDestination(base,b).path;
    assert(!alternate.empty() && alternate!=base && alternate!=suffix);
    assert(ExistingRecording(base,a,actual) && actual.bits==16);
    assert(DeleteFileW(base.c_str()) && DeleteFileW(suffix.c_str()));
    // URI enrichment must not strand a filename allocated from text identity.
    auto legacy=b;legacy.spotify_uri.clear();legacy.title=L"Collision";
    legacy.artist=L"Artist";legacy.album=L"Album";legacy.duration=16.0/44100;
    a.title=L"Unrelated";Write(base,Tagged(a,16));
    const auto legacy_suffix=ResolveRecordingDestination(base,legacy).path;
    assert(!legacy_suffix.empty() && legacy_suffix!=base);
    Write(legacy_suffix,Tagged(legacy,24));
    auto enriched=legacy;enriched.spotify_uri=b.spotify_uri;
    const auto legacy_match=ResolveRecordingDestination(base,enriched);
    assert(legacy_match.path==legacy_suffix&&!legacy_match.explicit_uid_conflict);
    assert(DeleteFileW(legacy_suffix.c_str()));Write(legacy_suffix,Tagged(enriched,24));
    assert(DeleteFileW(base.c_str()));
    assert(ResolveRecordingDestination(base,enriched).path==legacy_suffix && DecideSave(legacy_suffix,lower,enriched)==SaveDecision::Skip);
    assert(DeleteFileW(legacy_suffix.c_str()) && RemoveDirectoryW(root.c_str()));
    puts("PASS: bounded collision search reuses best matching suffix after deletion; upgrades target it; inaccessible/partial/unrelated files never create an authorized overwrite");
}
