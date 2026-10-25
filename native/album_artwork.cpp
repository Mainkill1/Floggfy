#include "album_artwork.h"
#include "image_info.h"
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include "async_log.h"
#include "history_settings.h"
#include <atomic>
#include <cstdio>
#endif
namespace history {
bool IsAlbumArtworkUrl(const std::string& url){
    const std::string prefix="https://i.scdn.co/image/";
    if(url.size()!=prefix.size()+40 || url.compare(0,prefix.size(),prefix))return false;
    return std::all_of(url.begin()+prefix.size(),url.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
void AlbumArtworkCache::Cache(const std::string& uri,const std::string& url){
    if(uri.size()>256||uri.compare(0,14,"spotify:track:")||uri.size()==14||!IsAlbumArtworkUrl(url))return;
    auto now=std::chrono::steady_clock::now();uint64_t order;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it=entries_.find(uri);
        if(it!=entries_.end()){
            const auto& e=it->second;
            if(e.pending || (e.url==url && (e.ready_url==url||now-e.attempted<std::chrono::seconds(60))))return;
        }else if(entries_.size()>=8){
            auto victim=entries_.end();
            for(auto p=entries_.begin();p!=entries_.end();++p)if(!p->second.pending&&(victim==entries_.end()||p->second.order<victim->second.order))victim=p;
            if(victim==entries_.end())return;
            entries_.erase(victim);
        }
        auto& e=entries_[uri];e.url=url;e.attempted=now;e.pending=true;e.order=order=++order_;
    }
    std::vector<uint8_t> bytes;ImageInfo info;bool ok=false;
    try{ok=fetch_(url,bytes)&&ReadImageInfo(bytes,info);}catch(...){ok=false;}
    std::lock_guard<std::mutex> lock(mutex_);auto it=entries_.find(uri);
    if(it==entries_.end()||it->second.order!=order)return;
    auto& e=it->second;e.pending=false;
    if(ok){e.bytes=std::move(bytes);e.mime=info.mime;e.ready_url=url;}
}
bool AlbumArtworkCache::Get(const std::string& uri,std::vector<uint8_t>& bytes,std::string& mime){
    std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
    if(!lock)return false;
    auto it=entries_.find(uri);if(it==entries_.end()||it->second.bytes.empty())return false;
    bytes=it->second.bytes;mime=it->second.mime;return true;
}
static bool Fetch(const std::string& url,std::vector<uint8_t>& bytes){
#ifdef _WIN32
    // This observes only Floggfy's own fixed-host request. Never log the URL,
    // response bytes, headers, cookies or user identifiers.
    struct Observation {
        bool active=NetworkingLoggingEnabled(),complete=false;
        DWORD status=0,failure=0;
        const char* stage="session";
        ULONGLONG started=0;
        unsigned long operation=0;
        void Emit(const char* direction,const char* phase,DWORD error) const noexcept {
            char line[240];std::snprintf(line,sizeof(line),
                "direction=%s source=enrichment service=artwork op=%lu phase=%s stage=%s code=%lu winerr=%lu elapsed_ms=%llu",
                direction,operation,phase,stage,status,error,GetTickCount64()-started);
            QueueNetworkingDiagnostic(line);
        }
        bool Fail() noexcept {failure=GetLastError();return false;}
        Observation() {
            if(!active)return;
            const auto error=GetLastError();
            static std::atomic<unsigned long> next{0};
            operation=next.fetch_add(1,std::memory_order_relaxed)+1;started=GetTickCount64();
            Emit("local","prepare",0);SetLastError(error);
        }
        ~Observation() {
            if(!active)return;
            const auto error=GetLastError();
            Emit(status?"in":"local",complete?"complete":"failed",failure);
            SetLastError(error);
        }
    } observation;
    struct Handle {HINTERNET h=nullptr;~Handle(){if(h)WinHttpCloseHandle(h);}};
    Handle session{WinHttpOpen(L"Floggfy artwork",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
    if(!session.h)return observation.Fail();
    observation.stage="timeouts";
    if(!WinHttpSetTimeouts(session.h,2000,2000,2000,2000))return observation.Fail();
    observation.stage="connect";
    Handle connection{WinHttpConnect(session.h,L"i.scdn.co",INTERNET_DEFAULT_HTTPS_PORT,0)};
    if(!connection.h)return observation.Fail();
    observation.stage="open-request";
    // Keep the exact validated /image/ path; never follow a server redirect.
    std::wstring path(url.begin()+17,url.end());
    Handle request{WinHttpOpenRequest(connection.h,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE)};
    if(!request.h)return observation.Fail();
    observation.stage="options";
    DWORD disabled=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION|WINHTTP_DISABLE_REDIRECTS;
    if(!WinHttpSetOption(request.h,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled)))return observation.Fail();
    observation.stage="send";
    if(!WinHttpSendRequest(request.h,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0))return observation.Fail();
    if(observation.active)observation.Emit("out","sent",0);
    observation.stage="receive";
    if(!WinHttpReceiveResponse(request.h,nullptr))return observation.Fail();
    observation.stage="http-status";
    DWORD status=0,size=sizeof(status);
    if(!WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX))return observation.Fail();
    observation.status=status;
    if(status!=200)return false;
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    uint8_t chunk[16384];DWORD read=0;
    observation.stage="read";
    for(;;){
        if(std::chrono::steady_clock::now()>deadline){observation.stage="deadline";return false;}
        if(!WinHttpReadData(request.h,chunk,sizeof(chunk),&read))return observation.Fail();
        if(!read){observation.complete=!bytes.empty();return observation.complete;}
        if(bytes.size()+read>4*1024*1024){observation.stage="size-limit";return false;}
        bytes.insert(bytes.end(),chunk,chunk+read);
    }
#else
    (void)url;(void)bytes;return false;
#endif
}
static AlbumArtworkCache& Cache(){static AlbumArtworkCache cache(Fetch);return cache;}
void CacheAlbumArtwork(const std::string& uri,const std::string& url){Cache().Cache(uri,url);}
bool GetAlbumArtwork(const std::string& uri,std::vector<uint8_t>& bytes,std::string& mime){return Cache().Get(uri,bytes,mime);}
}
