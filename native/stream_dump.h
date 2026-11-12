#pragma once
#include <windows.h>
#include <shlobj.h>
#include "async_log.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace history::stream_dump {
constexpr size_t kChunk=65536,kCapacity=64;
enum class Kind { Input,Playback,Metadata };
struct Record {
    Kind kind=Kind::Input;uintptr_t context=0;unsigned epoch=0;
    uint64_t sequence=0,utc=0,group=0;double started=0,finished=0;
    size_t length=0;unsigned part=0,parts=0;bool decoder_valid=true;
    char instance[129]{};std::array<unsigned char,kChunk> bytes{};
};
struct Identity {uintptr_t context=0;unsigned epoch=0;bool used=false;std::array<unsigned char,16> value{};};
struct Gap {
    Kind kind=Kind::Input;uintptr_t context=0;unsigned epoch=0;bool global=false,identity_unknown=false;
    uint64_t sequence=0,last_sequence=0,utc=0,last_utc=0,records=0,bytes=0;double mono=0;
};
struct State {
    std::wstring folder;std::unique_ptr<Record[]> records;
    HANDLE signal=nullptr,thread=nullptr;size_t head=0,tail=0,count=0,identity_next=0;
    uint64_t sequence=0,total_omitted=0;std::array<Gap,64> gaps{};size_t gap_head=0,gap_tail=0,gap_count=0;
    bool stopping=false;std::array<Identity,64> identities{};
};
inline SRWLOCK lock=SRWLOCK_INIT;
inline State* state=nullptr;
inline std::atomic<bool> enabled{false};
inline uint64_t last_session=0;
inline uint64_t Utc() noexcept {
    FILETIME value{};GetSystemTimeAsFileTime(&value);
    return (((uint64_t(value.dwHighDateTime)<<32)|value.dwLowDateTime)-116444736000000000ull)/10000;
}
inline std::wstring Stamp(uint64_t milliseconds) {
    const uint64_t ticks=milliseconds*10000+116444736000000000ull;
    FILETIME file{DWORD(ticks),DWORD(ticks>>32)};SYSTEMTIME time{};FileTimeToSystemTime(&file,&time);
    wchar_t out[40]{};swprintf(out,40,L"%04u-%02u-%02u_%02u-%02u-%02u.%03uZ",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds);
    return out;
}
inline std::string Json(const std::string& input) {
    std::string out="\"";for(const unsigned char c:input) {
        if(c=='"'||c=='\\'){out+='\\';out+=char(c);}
        else if(c<32){char escaped[7];snprintf(escaped,sizeof(escaped),"\\u%04x",c);out+=escaped;}
        else out+=char(c);
    }return out+'"';
}
inline std::string InstanceKey(std::string value) {
    if(value.size()!=32)return value;
    for(const unsigned char c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return value;
    for(auto& c:value)if(c>='A'&&c<='F')c=char(c-'A'+'a');
    return value;
}
inline std::string Hex(const unsigned char* bytes) {
    constexpr char digits[]="0123456789abcdef";std::string out;
    for(unsigned i=0;i<16;++i){out+=digits[bytes[i]>>4];out+=digits[bytes[i]&15];}return out;
}
inline bool Enabled() noexcept {return enabled.load(std::memory_order_acquire);}
inline void Omitted(State& s,size_t count,size_t bytes,Kind kind=Kind::Metadata,uintptr_t context=0,unsigned epoch=0,double mono=0) noexcept {
    Gap gap;gap.kind=kind;gap.context=context;gap.epoch=epoch;gap.global=kind==Kind::Metadata;gap.identity_unknown=kind==Kind::Playback;
    gap.sequence=++s.sequence;gap.last_sequence=s.sequence+(count?count-1:0);s.sequence=gap.last_sequence;
    gap.utc=gap.last_utc=Utc();gap.records=count;gap.bytes=bytes;gap.mono=mono;s.total_omitted+=count;
    if(s.gap_count) {
        auto& last=s.gaps[(s.gap_tail+s.gaps.size()-1)%s.gaps.size()];
        if(last.last_sequence+1==gap.sequence&&last.kind==kind&&last.context==context&&last.epoch==epoch) {
            last.last_sequence=gap.last_sequence;last.last_utc=gap.last_utc;last.records+=count;last.bytes+=bytes;SetEvent(s.signal);return;
        }
    }
    if(s.gap_count==s.gaps.size()) {
        auto& last=s.gaps[(s.gap_tail+s.gaps.size()-1)%s.gaps.size()];last.global=true;last.identity_unknown=true;
        for(auto& identity:s.identities)identity.used=false;
        last.last_sequence=gap.last_sequence;last.last_utc=gap.last_utc;last.records+=count;last.bytes+=bytes;
    } else {s.gaps[s.gap_tail]=gap;s.gap_tail=(s.gap_tail+1)%s.gaps.size();++s.gap_count;}
    SetEvent(s.signal);
}
inline Record& Next(State& s,Kind kind,uintptr_t context,unsigned epoch,double started,double finished) noexcept {
    auto& record=s.records[s.tail];record.kind=kind;record.context=context;record.epoch=epoch;
    record.sequence=++s.sequence;record.utc=Utc();record.started=started;record.finished=finished;
    record.length=0;record.group=record.sequence;record.part=record.parts=1;record.decoder_valid=true;record.instance[0]=0;
    s.tail=(s.tail+1)%kCapacity;++s.count;return record;
}
inline void Input(uintptr_t context,unsigned epoch,const unsigned char* bytes,size_t length,double started,double finished,bool valid=true) noexcept {
    if(!Enabled()||!length)return;
    AcquireSRWLockExclusive(&lock);auto* s=state;
    if(!s||s->stopping||!Enabled()){ReleaseSRWLockExclusive(&lock);return;}
    const auto needed=length/kChunk+bool(length%kChunk);
    if(!bytes||needed>kCapacity-s->count){Omitted(*s,needed,length,Kind::Input,context,epoch,finished);ReleaseSRWLockExclusive(&lock);return;}
    const auto group=s->sequence+1;unsigned part=0;
    while(length) {
        const auto amount=std::min(length,kChunk);auto& r=Next(*s,Kind::Input,context,epoch,started,finished);
        r.group=group;r.part=++part;r.parts=unsigned(needed);r.decoder_valid=valid;r.length=amount;
        memcpy(r.bytes.data(),bytes,amount);bytes+=amount;length-=amount;
    }
    SetEvent(s->signal);ReleaseSRWLockExclusive(&lock);
}
inline void Playback(uintptr_t context,unsigned epoch,const std::array<unsigned char,16>& value,double time) noexcept {
    if(!Enabled())return;
    AcquireSRWLockExclusive(&lock);auto* s=state;
    if(!s||s->stopping||!Enabled()){ReleaseSRWLockExclusive(&lock);return;}
    Identity* identity=nullptr;
    for(auto& candidate:s->identities)if(candidate.used&&candidate.context==context&&candidate.epoch==epoch){identity=&candidate;break;}
    if(identity&&identity->value==value){ReleaseSRWLockExclusive(&lock);return;}
    if(s->count==kCapacity){Omitted(*s,1,0,Kind::Playback,context,epoch,time);ReleaseSRWLockExclusive(&lock);return;}
    if(!identity)identity=&s->identities[s->identity_next++%s->identities.size()];
    *identity={context,epoch,true,value};auto& r=Next(*s,Kind::Playback,context,epoch,time,time);
    memcpy(r.bytes.data(),value.data(),value.size());r.length=value.size();
    SetEvent(s->signal);ReleaseSRWLockExclusive(&lock);
}
inline void Metadata(const std::string& instance,const std::string& uri,const std::string& title,
                     const std::string& artist,const std::string& album,double duration,double position,bool playing,double time,const char* status="ready") noexcept {
    if(!Enabled())return;
    try {
    const auto text="\"instance\":"+Json(instance)+",\"uri\":"+Json(uri)+",\"title\":"+Json(title)+
        ",\"artist\":"+Json(artist)+",\"album\":"+Json(album)+",\"duration\":"+std::to_string(duration)+
        ",\"position\":"+std::to_string(position)+",\"playing\":"+(playing?"true":"false")+",\"state\":"+Json(status);
    AcquireSRWLockExclusive(&lock);auto* s=state;
    if(!s||s->stopping||!Enabled()){ReleaseSRWLockExclusive(&lock);return;}
    if(text.size()>kChunk||instance.size()>128||s->count==kCapacity){Omitted(*s,1,0,Kind::Metadata,0,0,time);ReleaseSRWLockExclusive(&lock);return;}
    auto& r=Next(*s,Kind::Metadata,0,0,time,time);r.length=text.size();memcpy(r.bytes.data(),text.data(),text.size());
    memcpy(r.instance,instance.data(),instance.size());r.instance[instance.size()]=0;
    SetEvent(s->signal);ReleaseSRWLockExclusive(&lock);
    }catch(...) {
        AcquireSRWLockExclusive(&lock);if(state&&!state->stopping)Omitted(*state,1,0,Kind::Metadata,0,0,time);ReleaseSRWLockExclusive(&lock);
    }
}
inline bool Write(HANDLE file,const void* bytes,size_t size) {
    while(size){DWORD amount=0;if(!WriteFile(file,bytes,DWORD(size),&amount,nullptr)||!amount)return false;bytes=static_cast<const char*>(bytes)+amount;size-=amount;}return true;
}
inline bool Line(HANDLE file,const std::string& text) {const auto line=text+"}\n";return Write(file,line.data(),line.size());}
struct Stream {
    HANDLE input=INVALID_HANDLE_VALUE,log=INVALID_HANDLE_VALUE;
    uint64_t offset=0,logical=0,identity_unknown_through=0;bool incomplete=false,logical_known=true;unsigned id=0,epoch=0;std::string name,instance;
};
inline std::string Common(const Record& r,const char* kind) {
    return std::string("{\"kind\":\"")+kind+"\",\"sequence\":"+std::to_string(r.sequence)+
        ",\"utc_ms\":"+std::to_string(r.utc)+",\"started_mono\":"+std::to_string(r.started)+",\"finished_mono\":"+std::to_string(r.finished);
}
inline DWORD WINAPI Writer(void* parameter) {
    auto& s=*static_cast<State*>(parameter);HANDLE session=INVALID_HANDLE_VALUE;
    std::map<std::pair<uintptr_t,unsigned>,Stream> streams;std::map<std::string,std::string> metadata;
    bool ok=true;
    try {
        const auto result=SHCreateDirectoryExW(nullptr,s.folder.c_str(),nullptr);
        if(result!=ERROR_SUCCESS&&result!=ERROR_ALREADY_EXISTS&&result!=ERROR_FILE_EXISTS)throw 1;
        session=CreateFileW((s.folder+L"\\session.jsonl").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(session==INVALID_HANDLE_VALUE)throw 1;
        if(!Line(session,"{\"kind\":\"session_start\",\"utc_ms\":"+std::to_string(Utc())+",\"schema\":1,\"input_order\":\"callback-completion\",\"queue_bytes\":"+std::to_string(kCapacity*kChunk)))throw 1;
        bool global_incomplete=false,global_unknown_data=false;uint64_t global_identity_unknown_through=0;std::string latest_metadata;
        const auto obtain=[&](uintptr_t context,unsigned epoch,uint64_t utc)->Stream& {
            const auto key=std::make_pair(context,epoch);auto it=streams.find(key);
            if(it!=streams.end())return it->second;
            Stream stream;stream.id=unsigned(streams.size()+1);stream.epoch=epoch;stream.incomplete=global_incomplete;stream.logical_known=!global_unknown_data;stream.identity_unknown_through=global_identity_unknown_through;
            const auto stem=Stamp(utc)+L"-stream-"+std::to_wstring(stream.id);
            stream.name=std::string(stem.begin(),stem.end())+".bin";
            stream.input=CreateFileW((s.folder+L"\\"+stem+L".bin").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            stream.log=CreateFileW((s.folder+L"\\"+stem+L".jsonl").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            it=streams.emplace(key,std::move(stream)).first;
            if(it->second.input==INVALID_HANDLE_VALUE||it->second.log==INVALID_HANDLE_VALUE)throw 1;
            if(!latest_metadata.empty()&&!Line(it->second.log,latest_metadata+",\"offset\":0,\"association\":\"unresolved\""))throw 1;
            return it->second;
        };
        Record record;
        for(;;) {
            Gap gap;bool got=false,lost=false,stop=false;
            AcquireSRWLockExclusive(&lock);
            if(s.gap_count&&(!s.count||s.gaps[s.gap_head].sequence<s.records[s.head].sequence)) {
                gap=s.gaps[s.gap_head];s.gap_head=(s.gap_head+1)%s.gaps.size();--s.gap_count;lost=true;
            } else if(s.count){record=s.records[s.head];s.head=(s.head+1)%kCapacity;--s.count;got=true;}
            stop=s.stopping&&!s.count&&!s.gap_count;
            ReleaseSRWLockExclusive(&lock);
            if(lost) {
                const auto line=std::string("{\"kind\":\"gap\",\"sequence\":")+std::to_string(gap.sequence)+",\"last_sequence\":"+std::to_string(gap.last_sequence)+
                    ",\"utc_ms\":"+std::to_string(gap.utc)+",\"last_utc_ms\":"+std::to_string(gap.last_utc)+",\"mono\":"+std::to_string(gap.mono)+
                    ",\"records\":"+std::to_string(gap.records)+",\"bytes\":"+std::to_string(gap.bytes)+",\"source\":"+Json(gap.kind==Kind::Input?"input":gap.kind==Kind::Playback?"playback":"metadata")+",\"native_identity_unknown\":"+(gap.identity_unknown?"true":"false")+",\"complete\":false";
                if(gap.global) {
                    global_incomplete=true;if(gap.identity_unknown)global_identity_unknown_through=std::max(global_identity_unknown_through,gap.last_sequence);global_unknown_data=global_unknown_data||gap.bytes!=0;if(!Line(session,line+",\"scope\":\"all-streams\""))throw 1;
                    for(auto& item:streams){item.second.incomplete=true;if(global_unknown_data)item.second.logical_known=false;if(gap.identity_unknown){item.second.instance.clear();item.second.identity_unknown_through=std::max(item.second.identity_unknown_through,gap.last_sequence);}if(!Line(item.second.log,line+",\"scope\":\"all-streams\""))throw 1;}
                } else {
                    auto& stream=obtain(gap.context,gap.epoch,gap.utc);stream.incomplete=true;if(gap.kind==Kind::Playback){stream.instance.clear();stream.identity_unknown_through=std::max(stream.identity_unknown_through,gap.last_sequence);}
                    const auto located=line+",\"stream\":"+std::to_string(stream.id)+",\"epoch\":"+std::to_string(gap.epoch)+",\"offset\":"+std::to_string(stream.offset)+",\"logical_offset\":"+(stream.logical_known?std::to_string(stream.logical):"null")+",\"file\":"+Json(stream.name);
                    if(!Line(session,located)||!Line(stream.log,located))throw 1;
                    if(gap.kind==Kind::Input)stream.logical+=gap.bytes;
                }
            }
            if(got) {
                if(record.kind==Kind::Metadata) {
                    const auto line=Common(record,"metadata")+","+std::string(reinterpret_cast<const char*>(record.bytes.data()),record.length);
                    if(!Line(session,line))throw 1;
                    latest_metadata=line;metadata[InstanceKey(record.instance)]=line;if(metadata.size()>128)metadata.erase(metadata.begin());
                    for(auto& item:streams)if(item.second.instance.empty()||item.second.instance==InstanceKey(record.instance))
                        if(!Line(item.second.log,line+",\"offset\":"+std::to_string(item.second.offset)+",\"association\":"+Json(item.second.instance.empty()?"unresolved":"native-instance")))throw 1;
                } else {
                    auto& stream=obtain(record.context,record.epoch,record.utc);
                    auto line=Common(record,record.kind==Kind::Input?"input":"playback")+",\"stream\":"+std::to_string(stream.id)+
                        ",\"epoch\":"+std::to_string(stream.epoch)+",\"file\":"+Json(stream.name)+",\"offset\":"+std::to_string(stream.offset)+",\"logical_offset\":"+(stream.logical_known?std::to_string(stream.logical):"null")+",\"complete\":"+(stream.incomplete?"false":"true");
                    if(record.kind==Kind::Input) {
                        line+=",\"bytes\":"+std::to_string(record.length)+",\"group\":"+std::to_string(record.group)+",\"part\":"+std::to_string(record.part)+",\"parts\":"+std::to_string(record.parts)+",\"decoder_valid\":"+(record.decoder_valid?"true":"false");
                        if(!Write(stream.input,record.bytes.data(),record.length))throw 1;
                        stream.offset+=record.length;stream.logical+=record.length;
                    } else {const auto observed=Hex(record.bytes.data());const bool confirmed=record.sequence>stream.identity_unknown_through;stream.instance=confirmed?observed:std::string{};line+=",\"instance\":"+Json(observed)+",\"identity_confirmed\":"+(confirmed?"true":"false");}
                    if(!Line(session,line)||!Line(stream.log,line))throw 1;
                    if(record.kind==Kind::Playback&&!stream.instance.empty()) {
                        const auto cached=metadata.find(stream.instance);
                        if(cached!=metadata.end()&&!Line(stream.log,cached->second+",\"offset\":"+std::to_string(stream.offset)+",\"cached\":true,\"association\":\"native-instance\""))throw 1;
                    }
                }
            }
            if(stop)break;
            if(!got&&!lost)WaitForSingleObject(s.signal,200);
        }
        if(!Line(session,"{\"kind\":\"session_end\",\"utc_ms\":"+std::to_string(Utc())+",\"omitted_records\":"+std::to_string(s.total_omitted)+",\"complete\":"+(s.total_omitted?"false":"true")))throw 1;
    }catch(...){ok=false;enabled.store(false,std::memory_order_release);}
    for(auto& item:streams){if(item.second.input!=INVALID_HANDLE_VALUE)CloseHandle(item.second.input);if(item.second.log!=INVALID_HANDLE_VALUE)CloseHandle(item.second.log);}
    if(session!=INVALID_HANDLE_VALUE)CloseHandle(session);
    if(!ok)LogEvent(LogKind::Warning,"warning","DumpStreams write failed; diagnostic files may be incomplete");
    return ok?0:1;
}
inline bool Start(const std::wstring& root) {
    if(root.empty())return false;
    AcquireSRWLockExclusive(&lock);if(state){ReleaseSRWLockExclusive(&lock);return false;}
    std::unique_ptr<State> next;
    try {next=std::make_unique<State>();next->records=std::make_unique<Record[]>(kCapacity);const auto now=std::max(Utc(),last_session+1);last_session=now;next->folder=root+L"\\DumpStreams\\"+Stamp(now);}
    catch(...){ReleaseSRWLockExclusive(&lock);return false;}
    enabled.store(true,std::memory_order_release);
    next->signal=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(next->signal)next->thread=CreateThread(nullptr,0,Writer,next.get(),0,nullptr);
    if(!next->thread){enabled.store(false,std::memory_order_release);if(next->signal)CloseHandle(next->signal);ReleaseSRWLockExclusive(&lock);return false;}
    SetThreadPriority(next->thread,THREAD_PRIORITY_BELOW_NORMAL);state=next.release();ReleaseSRWLockExclusive(&lock);return true;
}
inline std::wstring SessionFolder() {AcquireSRWLockShared(&lock);auto folder=state?state->folder:std::wstring{};ReleaseSRWLockShared(&lock);return folder;}
inline bool Stop(DWORD timeout=5000) {
    enabled.store(false,std::memory_order_release);AcquireSRWLockExclusive(&lock);auto* current=state;
    if(!current){ReleaseSRWLockExclusive(&lock);return true;}
    current->stopping=true;SetEvent(current->signal);ReleaseSRWLockExclusive(&lock);
    if(WaitForSingleObject(current->thread,timeout)!=WAIT_OBJECT_0)return false;
    AcquireSRWLockExclusive(&lock);if(state==current)state=nullptr;ReleaseSRWLockExclusive(&lock);
    CloseHandle(current->thread);CloseHandle(current->signal);delete current;return true;
}
}
