// Exercise the production WinRT reader against deterministic COM boundaries.
// A stale first Spotify session must not hide a usable playback session.
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <cassert>
#include <cstdio>
static HRESULT FakeFactory(HSTRING,REFIID,void**);
#define RoGetActivationFactory FakeFactory
#include "../native/media_session.cpp"
#undef RoGetActivationFactory
namespace {
struct Object { void** table;unsigned refs=1;unsigned kind=0; };
void* factory_table[7]={};void* operation_table[9]={};void* info_table[11]={};
void* manager_table[8]={};void* sessions_table[8]={};void* session_table[10]={};
void* timeline_table[12]={};void* playback_table[8]={};void* properties_table[16]={};
Object factory{factory_table},operation{operation_table},info{info_table},manager{manager_table},sessions{sessions_table};
Object property_operation{operation_table,1,1};
Object stale{session_table,1,0},usable{session_table,1,1};
Object stale_timeline{timeline_table,1,0},usable_timeline{timeline_table,1,1};
Object playback{playback_table},properties{properties_table};
bool disconnected=false,all_invalid=false,missing_title=false;unsigned session_count=2,factory_calls=0;
HRESULT timeline_error=S_OK;
HRESULT Return(Object& object,void** out){++object.refs;*out=&object;return S_OK;}
ULONG STDMETHODCALLTYPE AddRef(Object* self){return ++self->refs;}
ULONG STDMETHODCALLTYPE Release(Object* self){assert(self->refs);return --self->refs;}
HRESULT STDMETHODCALLTYPE Query(Object* self,const GUID* iid,void** out){
    if(self==&operation || self==&property_operation)return Return(info,out);
    if(*iid==IID_IUnknown)return Return(*self,out);
    return E_NOINTERFACE;
}
HRESULT STDMETHODCALLTYPE Request(Object*,void** out){return Return(operation,out);}
HRESULT STDMETHODCALLTYPE Status(Object*,int* out){*out=1;return S_OK;}
HRESULT STDMETHODCALLTYPE Results(Object* self,void** out){return Return(self->kind?properties:manager,out);}
HRESULT STDMETHODCALLTYPE Sessions(Object*,void** out){if(disconnected)return RPC_E_DISCONNECTED;return Return(sessions,out);}
HRESULT STDMETHODCALLTYPE Count(Object*,unsigned* out){*out=session_count;return S_OK;}
HRESULT STDMETHODCALLTYPE At(Object*,unsigned at,void** out){return Return(at?usable:stale,out);}
HRESULT STDMETHODCALLTYPE App(Object*,HSTRING* out){return WindowsCreateString(L"Spotify.exe",11,out);}
HRESULT STDMETHODCALLTYPE Timeline(Object* self,void** out){return Return(self->kind?usable_timeline:stale_timeline,out);}
HRESULT STDMETHODCALLTYPE Playback(Object*,void** out){return Return(playback,out);}
HRESULT STDMETHODCALLTYPE Start(Object*,int64_t* out){*out=0;return S_OK;}
HRESULT STDMETHODCALLTYPE End(Object* self,int64_t* out){*out=self->kind&&!all_invalid?1800000000LL:0;return timeline_error;}
HRESULT STDMETHODCALLTYPE Position(Object*,int64_t* out){*out=0;return S_OK;}
HRESULT STDMETHODCALLTYPE Updated(Object*,int64_t* out){FILETIME now;GetSystemTimeAsFileTime(&now);*out=(uint64_t(now.dwHighDateTime)<<32)|now.dwLowDateTime;return S_OK;}
HRESULT STDMETHODCALLTYPE Playing(Object*,int* out){*out=4;return S_OK;}
HRESULT STDMETHODCALLTYPE MediaProperties(Object*,void** out){return Return(property_operation,out);}
HRESULT STDMETHODCALLTYPE Title(Object*,HSTRING* out){if(missing_title){*out=nullptr;return S_OK;}return WindowsCreateString(L"Usable track",12,out);}
HRESULT STDMETHODCALLTYPE Empty(Object*,HSTRING* out){*out=nullptr;return S_OK;}
HRESULT STDMETHODCALLTYPE NoGenres(Object*,void** out){*out=nullptr;return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE Track(Object*,int* out){*out=1;return S_OK;}
template<class F> void Slot(void** table,unsigned index,F function){table[index]=reinterpret_cast<void*>(function);}
void Setup(){
    for(auto* table:{factory_table,operation_table,info_table,manager_table,sessions_table,session_table,timeline_table,playback_table,properties_table}){
        Slot(table,0,Query);Slot(table,1,AddRef);Slot(table,2,Release);
    }
    Slot(factory_table,6,Request);Slot(operation_table,8,Results);Slot(info_table,7,Status);
    Slot(manager_table,7,Sessions);Slot(sessions_table,7,Count);Slot(sessions_table,6,At);
    Slot(session_table,6,App);Slot(session_table,7,MediaProperties);Slot(session_table,8,Timeline);Slot(session_table,9,Playback);
    Slot(timeline_table,6,Start);Slot(timeline_table,7,End);Slot(timeline_table,10,Position);Slot(timeline_table,11,Updated);
    Slot(playback_table,7,Playing);
    Slot(properties_table,6,Title);Slot(properties_table,8,Empty);Slot(properties_table,9,Empty);Slot(properties_table,10,Empty);
    Slot(properties_table,11,Track);Slot(properties_table,12,NoGenres);
}
}
static HRESULT FakeFactory(HSTRING,REFIID,void** out){++factory_calls;return Return(factory,out);}
int main(){
    Setup();
    // A dropped guard under real SRW contention must leave durable invalidation.
    AcquireSRWLockExclusive(&history::client_media_lock);
    history::PublishClientMediaUnavailable("remote-playback",true);
    ReleaseSRWLockExclusive(&history::client_media_lock);
    history::Media guarded;double guarded_time=0;uint64_t guarded_sequence=0;history::MediaDiagnostic guarded_diagnostic;
    assert(history::TryReadClientMedia(guarded,guarded_time,guarded_sequence,guarded_diagnostic)==history::ClientMediaRead::Blocked);
    assert(std::string(guarded_diagnostic.reason)=="client-observation-loss");
    history::MediaReader reader;history::Media media;double observed=0;
    const auto result=reader.Observe(media,observed);
    if(result!=history::MediaRead::Ready){std::fprintf(stderr,"FAIL: stale first Spotify session hides a usable playback timeline: %s\n",history::DescribeMediaDiagnostic(reader.Diagnostic()).c_str());return 1;}
    assert(media.title==L"Usable track"&&media.duration==180&&media.playing&&observed>0);
    assert(std::string(reader.Diagnostic().stage)=="ready"&&reader.Diagnostic().candidates==2);
    all_invalid=true;assert(reader.Observe(media,observed)==history::MediaRead::Failed);
    assert(std::string(reader.Diagnostic().reason)=="duration-unavailable");
    assert(reader.Diagnostic().sessions==2&&reader.Diagnostic().candidates==2&&media.title==L"Usable track");
    all_invalid=false;session_count=0;assert(reader.Observe(media,observed)==history::MediaRead::Failed);
    assert(std::string(reader.Diagnostic().reason)=="no-spotify-session");
    session_count=2;timeline_error=E_ACCESSDENIED;
    assert(reader.Observe(media,observed)==history::MediaRead::Failed);
    assert(std::string(reader.Diagnostic().stage)=="timeline-end"&&reader.Diagnostic().hresult==E_ACCESSDENIED);
    timeline_error=S_OK;missing_title=true;
    assert(reader.Observe(media,observed)==history::MediaRead::Failed);
    assert(std::string(reader.Diagnostic().reason)=="title-unavailable"&&!reader.Diagnostic().title_present);
    missing_title=false;
    disconnected=true;assert(reader.Observe(media,observed)==history::MediaRead::Failed);
    assert(std::string(reader.Diagnostic().stage)=="enumerate-sessions"&&reader.Diagnostic().hresult==RPC_E_DISCONNECTED);
    disconnected=false;assert(reader.Observe(media,observed)==history::MediaRead::Ready);
    assert(factory_calls==2); // A dead manager is recreated, rather than retained forever.
    history::MediaDiagnosticReporter reporter;history::MediaDiagnostic diagnostic;
    assert(reporter.Due(diagnostic,0)&&!reporter.Due(diagnostic,1));
    diagnostic.stage="timeline-validation";diagnostic.reason="duration-unavailable";
    assert(!reporter.Due(diagnostic,2)&&reporter.Due(diagnostic,30));
    diagnostic.stage="ready";diagnostic.reason="ok";
    assert(reporter.Due(diagnostic,31)&&!reporter.Due(diagnostic,100));
    diagnostic.stage="enumerate-sessions";diagnostic.hresult=RPC_E_DISCONNECTED;
    assert(reporter.Due(diagnostic,101)&&!reporter.Due(diagnostic,102));
    const auto description=history::DescribeMediaDiagnostic(diagnostic);
    assert(description.find("hr=0x80010108")!=std::string::npos&&description.find("Usable track")==std::string::npos);
    std::puts("PASS: usable Spotify session selection and disconnected manager recovery");
}
