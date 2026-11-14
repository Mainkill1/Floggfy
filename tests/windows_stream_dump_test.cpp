#include "../native/stream_dump.h"
#include "../native/async_log.h"
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <cassert>
#include <vector>
#include <iostream>
#include <sstream>
namespace history {void LogEvent(LogKind,const char*,const std::string&) {}}
static std::string Read(const std::filesystem::path& path) {std::ifstream f(path,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
// These checks fail if worker payloads are replaced with decoded-consumption
// bytes, empty lifecycle events are skipped, or capture loss poisons input logs.
static void CaptureJournal(const std::filesystem::path& root) {
    using namespace history::stream_dump;
    assert(Start(root.wstring()));const auto folder=std::filesystem::path(SessionFolder());
    const unsigned char accepted[]={0,255,17,0,85},consumed[]={9,8};
    const unsigned char frame[]={1,0,3,0};
    std::array<unsigned char,16> identity{};identity[0]=1;
    Playback(4096,7,identity,1);
    Metadata(Hex(identity.data()),"spotify:track:example","Unresolved capture title","Artist","Album",1,0,true,1);
    CaptureEvent(4096,7,101,"begin",nullptr,0,1.125);
    Input(4096,7,consumed,sizeof(consumed),1,2);
    CaptureEvent(4096,7,103,"bytes",accepted,2,2.125);
    CaptureEvent(4096,7,104,"bytes",accepted+2,3,2.25);
    CaptureEvent(4096,7,105,"frame",frame,4,2.5);
    CaptureEvent(4096,7,106,"end",nullptr,0,3);
    CaptureEvent(4096,8,201,"begin",nullptr,0,4);
    CaptureEvent(4096,8,202,"error",nullptr,0,4.5,9,2,123,456);
    assert(Stop());
    const auto session=Read(folder/L"session.jsonl");
    assert(session.find("\"capture_complete\":true")!=std::string::npos);
    std::vector<std::string> payloads;std::string first_log,second_log;
    for(const auto& entry:std::filesystem::directory_iterator(folder)) {
        if(entry.path().filename().wstring().find(L"-capture-")==std::wstring::npos)continue;
        if(entry.path().extension()==L".bin")payloads.push_back(Read(entry.path()));
        if(entry.path().extension()==L".jsonl") {
            auto log=Read(entry.path());
            if(log.find("\"epoch\":7")!=std::string::npos)first_log=log;else second_log=log;
        }
    }
    assert(payloads.size()==2);
    assert(std::find(payloads.begin(),payloads.end(),std::string("\0\xff\x11\0\x55\1\0\3\0",9))!=payloads.end());
    assert(std::find(payloads.begin(),payloads.end(),std::string{})!=payloads.end());
    const char* events[]={"begin","bytes","bytes","frame","end"};
    const uint64_t sequences[]={101,103,104,105,106},offsets[]={0,0,2,5,9},lengths[]={0,2,3,4,0};
    std::istringstream rows(first_log);std::string row;unsigned i=0;
    while(std::getline(rows,row))if(row.find("\"kind\":\"capture_event\"")!=std::string::npos) {
        assert(i<5);
        assert(row.find(std::string("\"event\":\"")+events[i]+"\"")!=std::string::npos);
        assert(row.find("\"producer_sequence\":"+std::to_string(sequences[i])+",")!=std::string::npos);
        assert(row.find("\"offset\":"+std::to_string(offsets[i])+",")!=std::string::npos);
        assert(row.find("\"bytes\":"+std::to_string(lengths[i])+",")!=std::string::npos);
        assert(row.find("\"context\":4096,")!=std::string::npos);
        assert(row.find("\"source\":\"flac-read-worker\"")!=std::string::npos);++i;
    }
    assert(i==5);
    assert(first_log.find("Unresolved capture title")!=std::string::npos);
    assert(first_log.find("\"association\":\"native-instance\"")==std::string::npos);
    assert(first_log.find("\"association\":\"unresolved\"")!=std::string::npos);
    assert(second_log.find("\"format\":9,\"error_source\":2,\"requested\":123,\"returned\":456")!=std::string::npos);

    auto pending=std::make_unique<State>();pending->folder=(root/L"DumpStreams"/L"capture-pressure").wstring();
    pending->records=std::make_unique<Record[]>(kCapacity);pending->signal=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(pending->signal);
    state=pending.get();enabled.store(true);
    // Leave the writer stopped so overflow is deterministic and producer calls
    // must return without waiting for it to consume anything.
    std::vector<unsigned char> maximum(65307);
    for(size_t n=0;n<maximum.size();++n)maximum[n]=static_cast<unsigned char>(n*37);
    CaptureEvent(4096,7,1,"bytes",maximum.data(),maximum.size(),0);
    for(unsigned n=1;n<kCapacity;++n)CaptureEvent(4096,7,n+1,"begin",nullptr,0,n);
    CaptureEvent(4096,7,100,"bytes",accepted,sizeof(accepted),100);
    Input(4096,7,consumed,sizeof(consumed),100,101);
    pending->stopping=true;enabled.store(false);assert(Writer(pending.get())==0);state=nullptr;CloseHandle(pending->signal);
    const auto pressure=Read(std::filesystem::path(pending->folder)/L"session.jsonl");
    assert(pressure.find("\"kind\":\"capture_gap\"")!=std::string::npos);
    assert(pressure.find("\"kind\":\"gap\"")!=std::string::npos);
    assert(pressure.find("\"capture_complete\":false")!=std::string::npos);
    bool input_gap=false,capture_gap=false;
    for(const auto& entry:std::filesystem::directory_iterator(pending->folder))if(entry.path().extension()==L".jsonl"&&entry.path().filename()!=L"session.jsonl") {
        const auto log=Read(entry.path());
        if(entry.path().filename().wstring().find(L"-capture-")!=std::wstring::npos) {
            capture_gap=true;assert(log.find("\"kind\":\"capture_gap\"")!=std::string::npos);
            assert(log.find("\"kind\":\"gap\"")==std::string::npos);
        } else {
            input_gap=true;assert(log.find("\"kind\":\"capture_gap\"")==std::string::npos);
        }
    }
    assert(input_gap&&capture_gap);
    for(const auto& entry:std::filesystem::directory_iterator(pending->folder))
        if(entry.path().extension()==L".bin"&&entry.path().filename().wstring().find(L"-capture-")!=std::wstring::npos)
            assert(Read(entry.path())==std::string(reinterpret_cast<const char*>(maximum.data()),maximum.size()));

    // Consumed-input loss and even saturation of capture loss bookkeeping must
    // retain separate namespaces and never invent missing payload bytes.
    pending=std::make_unique<State>();pending->folder=(root/L"DumpStreams"/L"capture-gap-saturation").wstring();
    pending->records=std::make_unique<Record[]>(kCapacity);pending->signal=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(pending->signal);
    state=pending.get();enabled.store(true);
    Input(4096,7,consumed,sizeof(consumed),0,1);
    for(unsigned n=0;n<65;++n)Omitted(*pending,1,100,Kind::Capture,10000+n,7,2,n+1);
    pending->stopping=true;enabled.store(false);assert(Writer(pending.get())==0);state=nullptr;CloseHandle(pending->signal);
    bool global_capture_gap=false;
    for(const auto& entry:std::filesystem::directory_iterator(pending->folder))if(entry.path().extension()==L".jsonl") {
        const auto log=Read(entry.path());
        if(entry.path().filename().wstring().find(L"-stream-")!=std::wstring::npos) {
            assert(log.find("gap")==std::string::npos);
            assert(log.find("\"complete\":true")!=std::string::npos);
        }
        if(log.find("\"scope\":\"all-capture-streams\"")!=std::string::npos)global_capture_gap=true;
    }
    assert(global_capture_gap);

    assert(Start(root.wstring()));const auto consumed_loss=std::filesystem::path(SessionFolder());
    Input(4096,7,reinterpret_cast<const unsigned char*>(1),5*1024*1024,0,1);
    CaptureEvent(4096,7,1,"begin",nullptr,0,1);
    assert(Stop());const auto consumed_loss_session=Read(consumed_loss/L"session.jsonl");
    assert(consumed_loss_session.find("\"kind\":\"gap\"")!=std::string::npos);
    assert(consumed_loss_session.find("\"capture_complete\":true")!=std::string::npos);

    // A file-creation failure must disable recording and leave explicit
    // incomplete evidence, never a successful session_end.
    pending=std::make_unique<State>();pending->folder=(root/L"capture-write-failure").wstring();
    pending->records=std::make_unique<Record[]>(kCapacity);pending->signal=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(pending->signal);
    state=pending.get();enabled.store(true);CaptureEvent(4096,7,1,"begin",nullptr,0,1);
    std::filesystem::create_directories(pending->folder);
    const auto collision=std::filesystem::path(pending->folder)/(Stamp(pending->records[0].utc)+L"-capture-1.bin");
    {std::ofstream preexisting(collision);assert(preexisting);preexisting<<"existing";}
    pending->stopping=true;assert(Writer(pending.get())==1);assert(!Enabled());state=nullptr;CloseHandle(pending->signal);
    const auto failure=Read(std::filesystem::path(pending->folder)/L"session.jsonl");
    assert(failure.find("\"kind\":\"write_error\"")!=std::string::npos);
    assert(failure.find("session_end")==std::string::npos);
    assert(Read(collision)=="existing");
}
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
    std::array<unsigned char,32> native_field{};native_field[0]=0x50;native_field[23]=1;
    Playback(0x1000,7,first,1,&native_field);Playback(0x1000,7,first,1.1,&native_field);
    Metadata("00000000000000000000000000000001","spotify:episode:0000000000000000000001","First poem","Poet","Book",30,0,true,1.1);
    Input(0x1000,7,a,sizeof(a),1.2,1.3);
    Playback(0x1000,7,second,2);
    Metadata("00000000000000000000000000000002","spotify:episode:0000000000000000000002","Second \"poem\"\n","Poet","Book",20,0,true,2.1);
    Input(0x1000,7,b,sizeof(b),2.2,2.3,false);
    Metadata("000000000000000000000000000000AB","spotify:episode:0000000000000000000003","Third poem","Other poet","Other book",10,0,true,3);
    Playback(0x2000,7,third,3.1);Input(0x2000,7,c,sizeof(c),3.2,3.3);
    Decision("capture event=queued");
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
    assert(session.find("\"message\":\"capture event=queued\"")!=std::string::npos);
    assert(session.find("\"native_field\":\"5000000000000000000000000000000000000000000000010000000000000000\"")!=std::string::npos);
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
    // Exercise a stopped writer deterministically: decisions cannot consume raw
    // slots or turn their own overflow into missing input/identity evidence.
    const auto decisions_folder=root/L"DumpStreams"/L"decision-pressure";
    auto pending=std::make_unique<State>();pending->folder=decisions_folder.wstring();
    pending->records=std::make_unique<Record[]>(kCapacity);
    pending->signal=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(pending->signal);
    state=pending.get();enabled.store(true);
    const std::string decision="capture reason=\"quoted\" path=C:\\audio\nnext\tline";
    for(unsigned i=0;i<100;++i)Decision(decision.c_str());
    std::vector<unsigned char> full_input(kCapacity*kChunk,0xab);
    Input(0x5000,9,full_input.data(),full_input.size(),6,7);
    Decision(std::string(kChunk+1,'x').c_str());
    assert(pending->count==kCapacity&&"decision logging must preserve the entire raw-input queue capacity");
    assert(pending->gap_count==0&&"decision omissions must not consume raw-gap or identity tracking");
    pending->stopping=true;enabled.store(false);
    assert(Writer(pending.get())==0);state=nullptr;CloseHandle(pending->signal);
    const auto decisions=Read(decisions_folder/L"session.jsonl");
    assert(decisions.find("\"message\":\"capture reason=\\\"quoted\\\" path=C:\\\\audio\\u000anext\\u0009line\"")!=std::string::npos);
    assert(decisions.find("\"kind\":\"decision_gap\"")!=std::string::npos);
    assert(decisions.find("\"kind\":\"gap\"")==std::string::npos);
    std::istringstream decision_rows(decisions);std::string row;
    uint64_t previous_sequence=0,written_decisions=0,omitted_decisions=0;
    while(std::getline(decision_rows,row)) {
        const auto sequence=row.find("\"sequence\":");
        if(sequence!=std::string::npos) {
            const auto current=std::stoull(row.substr(sequence+11));
            assert(current>previous_sequence&&"decisions and input must retain their sequence order");previous_sequence=current;
        }
        if(row.find("\"kind\":\"decision\"")!=std::string::npos)++written_decisions;
        if(row.find("\"kind\":\"decision_gap\"")!=std::string::npos) {
            const auto records=row.find("\"records\":");assert(records!=std::string::npos);
            omitted_decisions+=std::stoull(row.substr(records+10));
        }
    }
    assert(written_decisions&&omitted_decisions&&written_decisions+omitted_decisions==101);
    size_t retained_bytes=0;
    for(const auto& entry:std::filesystem::directory_iterator(decisions_folder))if(entry.path().extension()==L".bin") {
        const auto raw=Read(entry.path());retained_bytes+=raw.size();
        assert(raw==std::string(full_input.size(),char(0xab)));
    }
    assert(retained_bytes==full_input.size());
    assert(decisions.find("\"logical_offset\":4128768,\"complete\":true")!=std::string::npos);
    assert(decisions.find("\"kind\":\"session_end\"")!=std::string::npos);
    CaptureJournal(root);
    std::cout<<"PASS: opt-in raw input survives rejection, decoder reuse and metadata ordering; per-stream logs/offsets, bounded omissions and shutdown flush verified\n";
    return 0;
}
