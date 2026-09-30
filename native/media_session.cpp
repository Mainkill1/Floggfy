#include "media_session.h"
#include <roapi.h>
#include <winstring.h>
#include <algorithm>
#include <cstring>
#include <utility>
#include <cstdint>
#include <cwctype>
namespace history {
// These slots are the public WinRT COM ABI, copied from Windows SDK 26100.
// No Spotify UI internals, generated projections or runtime helpers are used.
template<class... A> static HRESULT Call(void* p, unsigned slot, A... args) {
    if(!p) return E_POINTER;
    auto f=reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(void*,A...)>((*static_cast<void***>(p))[slot]);
    return f(p,args...);
}
static void Release(void* p) { if(p) static_cast<IUnknown*>(p)->Release(); }
struct Com {
    void* p=nullptr;
    ~Com() { Release(p); }
    void** out() { Release(p); p=nullptr; return &p; }
};
static GUID Guid(const wchar_t* text) { GUID g={}; CLSIDFromString(text,&g); return g; }
static bool Query(void* p, const wchar_t* iid, Com& out) {
    auto id=Guid(iid); return SUCCEEDED(Call(p,0,&id,out.out()));
}
static bool Factory(const wchar_t* name, const wchar_t* iid, Com& out) {
    HSTRING text=nullptr; auto id=Guid(iid);
    if(FAILED(WindowsCreateString(name,static_cast<UINT32>(wcslen(name)),&text))) return false;
    HRESULT hr=RoGetActivationFactory(text,id,out.out()); WindowsDeleteString(text);
    return SUCCEEDED(hr);
}
static bool Await(void* operation, Com& result, unsigned results_slot=8) {
    Com info;
    if(!Query(operation,L"{00000036-0000-0000-c000-000000000046}",info)) return false;
    ULONGLONG end=GetTickCount64()+5000;
    for(;;) {
        int status=0; if(FAILED(Call(info.p,7,&status))) return false;
        if(status==1) return SUCCEEDED(Call(operation,results_slot,result.out()));
        if(status!=0) return false;
        if(GetTickCount64()>=end) { Call(info.p,9); return false; }
        Sleep(5);
    }
}
static std::wstring String(void* object, unsigned slot) {
    HSTRING text=nullptr; if(FAILED(Call(object,slot,&text))) return {};
    UINT32 n=0; const wchar_t* data=WindowsGetStringRawBuffer(text,&n);
    std::wstring result(data,n); WindowsDeleteString(text); return result;
}
std::string Utf8(const std::wstring& s) {
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,'\0');
    if(n) WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);
    return out;
}
std::string Json(const std::string& s) {
    const char* hex="0123456789abcdef"; std::string out="\"";
    for(unsigned char c:s) {
        if(c=='"' || c=='\\') { out+='\\'; out+=char(c); }
        else if(c<32) { out+="\\u00"; out+=hex[c>>4]; out+=hex[c&15]; }
        else out+=char(c);
    }
    return out+'"';
}
std::string Media::Key() const {
    return Utf8(title)+"\x1f"+Utf8(artist)+"\x1f"+Utf8(album);
}
static bool Artwork(void* properties, Media& out) {
    Com reference,operation,stream,random,input,factory,buffer,read,returned,access;
    if(FAILED(Call(properties,15,reference.out())) || !reference.p ||
       FAILED(Call(reference.p,6,operation.out())) || !Await(operation.p,stream) ||
       !Query(stream.p,L"{905a0fe1-bc53-11df-8c49-001e4fc686da}",random)) return false;
    uint64_t size=0;
    if(FAILED(Call(random.p,6,&size)) || size==0 || size>4*1024*1024 ||
       FAILED(Call(random.p,8,uint64_t(0),input.out())) ||
       !Factory(L"Windows.Storage.Streams.Buffer",L"{71af914d-c10f-484b-bc50-14bc623b3a27}",factory) ||
       FAILED(Call(factory.p,6,uint32_t(size),buffer.out())) ||
       FAILED(Call(input.p,6,buffer.p,uint32_t(size),int(0),read.out())) ||
       !Await(read.p,returned,10)) return false;
    uint32_t length=0; unsigned char* bytes=nullptr;
    if(FAILED(Call(returned.p,7,&length)) || length!=size ||
       !Query(returned.p,L"{905a0fef-bc53-11df-8c49-001e4fc686da}",access) ||
       FAILED(Call(access.p,3,&bytes))) return false;
    if(length>=3 && bytes[0]==0xff && bytes[1]==0xd8 && bytes[2]==0xff) out.cover_extension=L".jpg";
    else if(length>=8 && !std::memcmp(bytes,"\x89PNG\r\n\x1a\n",8)) out.cover_extension=L".png";
    else return false;
    out.cover.assign(bytes,bytes+length); return true;
}
MediaReader::~MediaReader() {
    Release(manager_); if(initialized_) RoUninitialize();
}
bool MediaReader::Read(Media& out) {
    if(!initialized_) {
        HRESULT hr=RoInitialize(RO_INIT_MULTITHREADED);
        if(FAILED(hr)) return false;
        initialized_=true;
    }
    if(!manager_) {
        Com factory,operation,manager;
        if(!Factory(L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager",
                    L"{2050c4ee-11a0-57de-aed7-c97c70338245}",factory) ||
           FAILED(Call(factory.p,6,operation.out())) || !Await(operation.p,manager)) return false;
        manager_=manager.p; manager.p=nullptr;
    }
    Com sessions,session;
    if(FAILED(Call(manager_,7,sessions.out()))) return false;
    unsigned count=0; if(FAILED(Call(sessions.p,7,&count))) return false;
    std::wstring app;
    for(unsigned i=0;i<count;i++) {
        if(FAILED(Call(sessions.p,6,i,session.out()))) continue;
        app=String(session.p,6);
        std::wstring lower=app;
        std::transform(lower.begin(),lower.end(),lower.begin(),[](wchar_t c){return wchar_t(towlower(c));});
        if(lower.find(L"spotify")!=std::wstring::npos) break;
        session.out();
    }
    if(!session.p) return false;
    Com operation,properties,timeline,playback;
    if(FAILED(Call(session.p,7,operation.out())) || !Await(operation.p,properties) ||
       FAILED(Call(session.p,8,timeline.out())) || FAILED(Call(session.p,9,playback.out()))) return false;
    Media media; media.app=app;
    media.title=String(properties.p,6); media.album_artist=String(properties.p,8);
    media.artist=String(properties.p,9); media.album=String(properties.p,10);
    Com genres;
    if(SUCCEEDED(Call(properties.p,12,genres.out())) && genres.p) {
        unsigned n=0;
        if(SUCCEEDED(Call(genres.p,7,&n)) && n<=20) for(unsigned i=0;i<n;i++) {
            HSTRING value=nullptr;
            if(SUCCEEDED(Call(genres.p,6,i,&value))) {
                UINT32 length=0;const wchar_t* text=WindowsGetStringRawBuffer(value,&length);
                if(length && length<=512){if(!media.genre.empty())media.genre+=L"; ";media.genre.append(text,length);}
                WindowsDeleteString(value);
            }
        }
    }
    int track=0; Call(properties.p,11,&track); media.track=track>0 ? unsigned(track) : 0;
    int status=0; Call(playback.p,7,&status); media.playing=status==4;
    int64_t start=0,end=0,pos=0,updated=0;
    Call(timeline.p,6,&start); Call(timeline.p,7,&end);
    Call(timeline.p,10,&pos); Call(timeline.p,11,&updated);
    media.duration=double(end-start)/10000000.0;
    media.position=double(pos-start)/10000000.0;
    FILETIME now; GetSystemTimeAsFileTime(&now);
    uint64_t ticks=(uint64_t(now.dwHighDateTime)<<32)|now.dwLowDateTime;
    double elapsed=(double(ticks)-double(updated))/10000000.0;
    if(media.playing && elapsed>=0 && elapsed<86400) media.position+=elapsed;
    media.position=std::max(0.0,std::min(media.position,media.duration));
    if(media.title.empty() || media.duration<=0) return false;
    if(media.Key()==cached_.Key() && !cached_.cover.empty()) {
        media.cover=cached_.cover; media.cover_extension=cached_.cover_extension;
    } else Artwork(properties.p,media);
    cached_=media; out=std::move(media); return true;
}
}
