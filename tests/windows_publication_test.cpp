#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "recording_fixture.h"
#include "../native/file_publication.h"
#include "../native/flac_history_core.h"
#include <cstdio>
#include <cstdlib>
using namespace history;
namespace history { std::wstring WindowsPath(const std::wstring& value) {
    if(value.rfind(L"\\\\?\\",0)==0) return value;
    if(value.rfind(L"\\\\",0)==0) return L"\\\\?\\UNC\\"+value.substr(2);
    if(value.size()>2 && value[1]==L':') return L"\\\\?\\"+value;
    return value;
} }
static void check(bool value,const char* label) { if(!value) { std::fprintf(stderr,"FAIL: %s (error %lu)\n",label,GetLastError()); std::exit(1); } }
static void write(const std::wstring& path,const std::vector<uint8_t>& bytes) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    DWORD written=0;check(file!=INVALID_HANDLE_VALUE&&WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size(),"fixture write");CloseHandle(file);
}
static std::vector<uint8_t> tagged(const Catalog& catalog,unsigned bits=16) {
    Tags tags; tags.fields={{"TITLE","Track"},{"ARTIST","Artist"},{"ALBUM","Album"},{"SPOTIFY_URI",catalog.spotify_uri},{"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_CAPTURE","native compressed FLAC input"}};
    std::vector<uint8_t> result;std::string error;
    check(TagFlac(recording_fixture::fixture(44100,bits),tags,result,error),"tag fixture");return result;
}
static std::wstring test_mode;
namespace history {
void PublicationTestPhase(const wchar_t* phase,const std::wstring& path) {
    if((test_mode==L"crash" && std::wstring(phase)==L"reserved") ||
       (test_mode==L"crash-new" && std::wstring(phase)==L"staged")) TerminateProcess(GetCurrentProcess(),77);
    if((test_mode==L"race-new" && std::wstring(phase)==L"staged") ||
       (test_mode==L"race-upgrade" && std::wstring(phase)==L"reserved")) write(path,{1,2,3});
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc==3 && (std::wstring(argv[1])==L"--crash" || std::wstring(argv[1])==L"--crash-new")) {
        Catalog catalog;catalog.spotify_uri="spotify:track:one";
        auto bytes=tagged(catalog,24);test_mode=std::wstring(argv[1]).substr(2);
        PublishBytes(argv[2],Quality{Codec::Flac,44100,2,24,0},bytes.data(),bytes.size(),catalog);
        return 99;
    }
    wchar_t temporary[MAX_PATH];check(GetTempPathW(MAX_PATH,temporary)>0,"temp path");
    auto root=std::wstring(temporary)+L"Floggfy-publication-"+std::to_wstring(GetCurrentProcessId());
    check(CreateDirectoryW(root.c_str(),nullptr)!=0,"isolated test directory");
    Catalog catalog;catalog.title=L"Track";catalog.album=L"Album";catalog.artist=L"Artist";catalog.spotify_uri="spotify:track:one";
    auto bytes=tagged(catalog),high=tagged(catalog,24);
    Quality quality{Codec::Flac,44100,2,16,0},better{Codec::Flac,44100,2,24,0},actual;
    auto path=root+L"\\Track.flac";
    check(PublishBytes(path,quality,bytes.data(),bytes.size(),catalog)==FilePublication::Saved,"first save stages and publishes");
    check(ExistingRecording(path,catalog,actual)&&actual.bits==16,"first published file is complete matching recording");
    check(PublishBytes(path,quality,bytes.data(),bytes.size(),catalog)==FilePublication::Skipped,"same recording and quality skipped");
    check(PublishBytes(path,better,high.data(),high.size(),catalog)==FilePublication::Upgraded,"quality upgrade safely promoted");
    check(ExistingRecording(path,catalog,actual)&&actual.bits==24,"upgrade published complete recording");
    auto other=catalog;other.spotify_uri="spotify:track:two";
    check(PublishBytes(path,better,high.data(),high.size(),other)==FilePublication::Failed,"mismatched incoming recording rejected");
    auto collision=ResolveRecordingPath(path,other);
    check(collision!=path&&!collision.empty()&&collision==ResolveRecordingPath(path,other),"stable alternate destination for different recording");
    auto different=tagged(other);
    check(PublishBytes(collision,quality,different.data(),different.size(),other)==FilePublication::Saved,"different recording saved separately");
    auto partial=root+L"\\partial.flac";auto broken=bytes;broken.pop_back();write(partial,broken);
    check(!ReadQuality(partial,actual),"partial owned FLAC never suppresses complete save");
    check(ResolveRecordingPath(partial,catalog)==partial,"matching old Floggfy partial can be repaired");
    check(PublishBytes(partial,quality,bytes.data(),bytes.size(),catalog)==FilePublication::Upgraded,"owned partial repaired even at equal quality");
    auto unknown=root+L"\\unknown.flac";auto header=recording_fixture::fixture();header.resize(42);write(unknown,header);
    check(!ReadQuality(unknown,actual)&&ResolveRecordingPath(unknown,catalog)!=unknown,"unowned header-only file preserved and disambiguated");
    check(PublishBytes(unknown,quality,bytes.data(),bytes.size(),catalog)==FilePublication::Skipped,"unknown file never overwritten");
    auto locked=root+L"\\locked.flac";write(locked,bytes);
    HANDLE lock=CreateFileW(locked.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    check(lock!=INVALID_HANDLE_VALUE,"lock target");
    check(PublishBytes(locked,better,high.data(),high.size(),catalog)==FilePublication::Failed,"locked target prevents upgrade without damaging old file");CloseHandle(lock);
    check(ReadQuality(locked,actual)&&actual.bits==16,"locked target unchanged");
    auto raced_new=root+L"\\raced-new.flac";
    test_mode=L"race-new";
    check(PublishBytes(raced_new,quality,bytes.data(),bytes.size(),catalog)==FilePublication::Failed,"unexpected first-save destination never overwritten");
    test_mode.clear();
    check(GetFileAttributesW(raced_new.c_str())!=INVALID_FILE_ATTRIBUTES&&!ReadQuality(raced_new,actual),"racing user's file survives first save");
    auto raced_upgrade=root+L"\\raced-upgrade.flac";write(raced_upgrade,bytes);
    test_mode=L"race-upgrade";
    check(PublishBytes(raced_upgrade,better,high.data(),high.size(),catalog)==FilePublication::Failed,"unexpected upgrade destination never overwritten");
    test_mode.clear();
    check(!ReadQuality(raced_upgrade,actual),"racing user's upgrade target survives rollback failure");
    auto recovered_race=ResolveRecordingPath(raced_upgrade,catalog);
    check(recovered_race!=raced_upgrade&&ExistingRecording(recovered_race,catalog,actual),"old recording recovered beside racing target on next operation");
    auto retained=root+L"\\retained.flac";write(retained,bytes);write(retained+L".previous",bytes);
    check(PublishBytes(retained,better,high.data(),high.size(),catalog)==FilePublication::Upgraded,
          "same-recording raced target and redundant backup do not block later upgrade");
    check(ExistingRecording(retained,catalog,actual)&&actual.bits==24&&GetFileAttributesW((retained+L".previous").c_str())==INVALID_FILE_ATTRIBUTES,
          "later upgrade retains best audio and retires redundant owned backup");
    auto preserved=root+L"\\preserved.flac";write(preserved,bytes);write(preserved+L".previous",high);
    check(PublishBytes(preserved,better,high.data(),high.size(),catalog)==FilePublication::Upgraded,
          "better reserved backup preserved while later upgrade succeeds");
    auto recovered_best=root+L"\\preserved [recovered].flac";
    check(ExistingRecording(recovered_best,catalog,actual)&&actual.bits==24,"better old backup survives as usable audio");
    auto retained_partial=root+L"\\retained-partial.flac";write(retained_partial,broken);write(retained_partial+L".previous",bytes);
    check(PublishBytes(retained_partial,quality,bytes.data(),bytes.size(),catalog)==FilePublication::Upgraded,
          "owned partial target plus retained backup can be repaired");
    auto recovered_partial=root+L"\\retained-partial [recovered].flac";
    check(ExistingRecording(retained_partial,catalog,actual)&&ExistingRecording(recovered_partial,catalog,actual),
          "complete backup retained while partial current target is repaired");
    auto interrupted=root+L"\\interrupted.flac";write(interrupted,bytes);
    wchar_t executable[MAX_PATH];check(GetModuleFileNameW(nullptr,executable,MAX_PATH)>0,"child executable path");
    auto command=L"\""+std::wstring(executable)+L"\" --crash \""+interrupted+L"\"";
    STARTUPINFOW startup={};startup.cb=sizeof(startup);PROCESS_INFORMATION process={};
    check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)!=0,"start interrupted publisher child");
    check(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0,"interrupted publisher exits");
    DWORD status=0;GetExitCodeProcess(process.hProcess,&status);CloseHandle(process.hThread);CloseHandle(process.hProcess);
    check(status==77&&GetFileAttributesW(interrupted.c_str())==INVALID_FILE_ATTRIBUTES,"terminated after old recording reserved");
    check(ResolveRecordingPath(interrupted,catalog)==interrupted&&ExistingRecording(interrupted,catalog,actual)&&actual.bits==16,"next operation restores old recording after process termination");
    WIN32_FIND_DATAW abandoned={};HANDLE stages=FindFirstFileW((interrupted+L".complete-*").c_str(),&abandoned);
    if(stages!=INVALID_HANDLE_VALUE) { do {DeleteFileW((root+L"\\"+abandoned.cFileName).c_str());}while(FindNextFileW(stages,&abandoned));FindClose(stages); }
    auto interrupted_new=root+L"\\interrupted-new.flac";
    command=L"\""+std::wstring(executable)+L"\" --crash-new \""+interrupted_new+L"\"";
    check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)!=0,"start interrupted first-save child");
    check(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0,"interrupted first-save publisher exits");
    GetExitCodeProcess(process.hProcess,&status);CloseHandle(process.hThread);CloseHandle(process.hProcess);
    check(status==77&&GetFileAttributesW(interrupted_new.c_str())==INVALID_FILE_ATTRIBUTES,"first-save interruption never exposes final path");
    check(PublishBytes(interrupted_new,quality,bytes.data(),bytes.size(),catalog)==FilePublication::Saved,
          "next complete listen can publish after interrupted first save");
    stages=FindFirstFileW((interrupted_new+L".complete-*").c_str(),&abandoned);
    if(stages!=INVALID_HANDLE_VALUE) { do {DeleteFileW((root+L"\\"+abandoned.cFileName).c_str());}while(FindNextFileW(stages,&abandoned));FindClose(stages); }
    auto invalid=root+L"\\invalid.flac";
    check(PublishBytes(invalid,quality,broken.data(),broken.size(),catalog)==FilePublication::Failed&&GetFileAttributesW(invalid.c_str())==INVALID_FILE_ATTRIBUTES,"incomplete incoming file never exposed");
    WIN32_FIND_DATAW data={};HANDLE listing=FindFirstFileW((root+L"\\*.complete-*").c_str(),&data);
    check(listing==INVALID_HANDLE_VALUE,"no staged files remain after handled outcomes");
    listing=FindFirstFileW((root+L"\\*.previous").c_str(),&data);check(listing==INVALID_HANDLE_VALUE,"successful upgrades remove protected backup");
    for(const auto& file:{path,collision,partial,unknown,locked,raced_new,raced_upgrade,recovered_race,interrupted,retained,preserved,recovered_best,interrupted_new,retained_partial,recovered_partial})DeleteFileW(file.c_str());
    check(RemoveDirectoryW(root.c_str())!=0,"test cleanup");
    std::puts("PASS: identity-safe publication, first-save staging, partial recovery, collisions and locked files");
    return 0;
}
