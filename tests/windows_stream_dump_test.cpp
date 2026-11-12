#include "../native/stream_dump.h"
#include "../native/async_log.h"
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <cassert>
#include <vector>
#include <iostream>
namespace history {void LogEvent(LogKind,const char*,const std::string&) {}}
static std::string Read(const std::filesystem::path& path) {std::ifstream f(path,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
int wmain(int argc,wchar_t** argv) {
    using namespace history::stream_dump;
    wchar_t temp[MAX_PATH]{};assert(GetTempPathW(MAX_PATH,temp));
    const auto root=argc==2?std::filesystem::path(argv[1]):std::filesystem::path(temp)/(L"Floggfy-dump-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    Input(1,0,reinterpret_cast<const unsigned char*>(1),100,0,1);
    assert(!Enabled()&&!std::filesystem::exists(root/L"DumpStreams"));
    assert(Start(root.wstring()));assert(!Start(root.wstring()));
    const auto folder=std::filesystem::path(SessionFolder());
    std::array<unsigned char,16> first{},second{},third{};first.back()=1;second.back()=2;third.back()=0xab;
    const unsigned char a[]={0,1,2},b[]={3,4,5},c[]={9,8};
    Playback(0x1000,7,first,1);Playback(0x1000,7,first,1.1);
    Metadata("00000000000000000000000000000001","spotify:episode:0000000000000000000001","First poem","Poet","Book",30,0,true,1.1);
    Input(0x1000,7,a,sizeof(a),1.2,1.3);
    Playback(0x1000,7,second,2);
    Metadata("00000000000000000000000000000002","spotify:episode:0000000000000000000002","Second \"poem\"\n","Poet","Book",20,0,true,2.1);
    Input(0x1000,7,b,sizeof(b),2.2,2.3,false);
    Metadata("000000000000000000000000000000AB","spotify:episode:0000000000000000000003","Third poem","Other poet","Other book",10,0,true,3);
    Playback(0x2000,7,third,3.1);Input(0x2000,7,c,sizeof(c),3.2,3.3);
    assert(Stop()&&!Enabled());
    std::vector<std::string> inputs,logs;
    for(const auto& entry:std::filesystem::directory_iterator(folder)) {
        if(entry.path().extension()==L".bin")inputs.push_back(Read(entry.path()));
        if(entry.path().filename()!=L"session.jsonl"&&entry.path().extension()==L".jsonl")logs.push_back(Read(entry.path()));
    }
    assert(inputs.size()==2&&logs.size()==2);
    assert(std::find(inputs.begin(),inputs.end(),std::string("\0\1\2\3\4\5",6))!=inputs.end());
    assert(std::find(inputs.begin(),inputs.end(),std::string("\11\10",2))!=inputs.end());
    bool paired=false,third_paired=false;
    for(const auto& log:logs) {
        paired=paired||(log.find("First poem")!=std::string::npos&&log.find("Second")!=std::string::npos&&log.find("\"offset\":3")!=std::string::npos&&log.find("\"decoder_valid\":false")!=std::string::npos);
        third_paired=third_paired||(log.find("Third poem")!=std::string::npos&&log.find("\"association\":\"native-instance\"")!=std::string::npos);
    }
    assert(paired&&third_paired);
    const auto session=Read(folder/L"session.jsonl");assert(session.find("session_end")!=std::string::npos&&session.find("gap")==std::string::npos);
    assert(Start(root.wstring()));const auto overflow_folder=std::filesystem::path(SessionFolder());
    Input(1,0,reinterpret_cast<const unsigned char*>(1),5*1024*1024,0,1);
    Input(1,0,a,sizeof(a),1,2);
    assert(Stop());assert(Read(overflow_folder/L"session.jsonl").find("\"kind\":\"gap\"")!=std::string::npos);
    bool located=false;
    for(const auto& entry:std::filesystem::directory_iterator(overflow_folder))if(entry.path().extension()==L".jsonl"&&entry.path().filename()!=L"session.jsonl") {
        const auto log=Read(entry.path());located=log.find("\"logical_offset\":5242880")!=std::string::npos&&log.find("\"stream\":1")!=std::string::npos&&log.find("\"epoch\":0")!=std::string::npos;
    }
    assert(located&&"omitted bytes must retain stream identity and an explicit logical hole");
    assert(Start(root.wstring()));const auto lost_identity_folder=std::filesystem::path(SessionFolder());
    Playback(0x1000,7,first,4);
    Metadata("00000000000000000000000000000001","spotify:episode:0000000000000000000001","Before gap","Poet","Book",30,0,true,4.1);
    AcquireSRWLockExclusive(&history::stream_dump::lock);
    Omitted(*history::stream_dump::state,1,0,Kind::Playback,0x1000,7,4.2);
    ReleaseSRWLockExclusive(&history::stream_dump::lock);
    Metadata("00000000000000000000000000000001","spotify:episode:0000000000000000000001","After gap","Poet","Book",30,1,true,4.3);
    assert(Stop());bool unresolved=false;
    for(const auto& entry:std::filesystem::directory_iterator(lost_identity_folder))if(entry.path().extension()==L".jsonl"&&entry.path().filename()!=L"session.jsonl") {
        std::ifstream log(entry.path());std::string line;while(std::getline(log,line))if(line.find("After gap")!=std::string::npos)
            unresolved=line.find("\"association\":\"unresolved\"")!=std::string::npos;
    }
    assert(unresolved&&"a missing native transition cannot leave a falsely confirmed metadata association");
    assert(Start(root.wstring()));const auto saturated=std::filesystem::path(SessionFolder());
    AcquireSRWLockExclusive(&history::stream_dump::lock);
    for(unsigned i=0;i<64;++i)Omitted(*history::stream_dump::state,1,1,Kind::Input,0x3000+i,7,5);
    auto& retained=Next(*history::stream_dump::state,Kind::Playback,0x1000,7,5.1,5.1);
    memcpy(retained.bytes.data(),first.data(),first.size());retained.length=first.size();
    Omitted(*history::stream_dump::state,1,0,Kind::Playback,0x4000,7,5.2);
    ReleaseSRWLockExclusive(&history::stream_dump::lock);
    Metadata("00000000000000000000000000000001","spotify:episode:0000000000000000000001","After saturated gap","Poet","Book",30,1,true,5.3);
    assert(Stop());bool saw_saturated=false;
    for(const auto& entry:std::filesystem::directory_iterator(saturated))if(entry.path().extension()==L".jsonl"&&entry.path().filename()!=L"session.jsonl") {
        std::ifstream log(entry.path());std::string line;while(std::getline(log,line))if(line.find("After saturated gap")!=std::string::npos) {
            saw_saturated=true;assert(line.find("\"association\":\"unresolved\"")!=std::string::npos);
        }
    }
    assert(saw_saturated&&"a retained marker inside an unknown loss interval cannot restore native identity confidence");
    std::cout<<"PASS: opt-in raw input survives rejection, decoder reuse and metadata ordering; per-stream logs/offsets, bounded omissions and shutdown flush verified\n";
    return 0;
}
