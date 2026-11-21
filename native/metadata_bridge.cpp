#define WIN32_LEAN_AND_MEAN
#include "metadata_bridge.h"
#include "telemetry_filter.h"
#include "hook_init_state.h"
#include "hook_rollback.h"
#include "hook_startup_minhook.h"
#include "rich_metadata.h"
#include "client_playback.h"
#include "playback_quality.h"
#include "cached_metadata.h"
#include "album_artwork.h"
#include "cached_artwork_windows.h"
#include "artwork_image.h"
#include "artwork_reply.h"
#include "image_info.h"
#include "history_settings.h"
#include "collector_config.h"
#include "marked_catalog.h"
#include "marked_protocol.h"
#include "async_log.h"
#include "srw_guard.h"
#include "vendor/minhook/include/MinHook.h"
#include "../build/metadata_script.h"
#include "../build/telemetry_script.h"
#include <atomic>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <new>
#include <utility>
namespace history {
namespace {
SRWLOCK current_item_lock=SRWLOCK_INIT;
Media current_item;
ULONGLONG current_item_updated=0;
void PublishCurrentItem(Media media) {
 ExclusiveSrw guard(current_item_lock);
 current_item=std::move(media);current_item_updated=GetTickCount64();
}
}
bool ReadCurrentItemMedia(Media& media) {
 if(!TryAcquireSRWLockShared(&current_item_lock))return false;
 struct Unlock {~Unlock(){ReleaseSRWLockShared(&current_item_lock);}} unlock;
 if(!current_item_updated || GetTickCount64()-current_item_updated>3000 || current_item.title.empty())return false;
 media=current_item;return true;
}
static std::wstring MetadataWide(const std::string& text) {
 if(text.empty()||text.size()>4096)return {};
 const auto length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
 if(!length)return {};
 std::wstring result(size_t(length),L'\0');
 if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),result.data(),length)!=length)return {};
 return result;
}
namespace {
struct Base {size_t size;void(*add)(Base*);int(*release)(Base*);int(*one)(Base*);int(*any)(Base*);};
struct String {wchar_t* str;size_t length;void(*dtor)(wchar_t*);};
template<size_t N> struct Object {Base base;void* methods[N];};
using Client=Object<19>;using Display=Object<13>;using Load=Object<4>;using Browser=Object<21>;using Frame=Object<26>;
static void Release(void* p) {if(p)static_cast<Base*>(p)->release(static_cast<Base*>(p));}
// CEF's C++ wrapper has private identity fields before the public C structure.
// Replacing that structure breaks GetClient() round trips (CEF UnwrapDerived).
// Intercept callback code instead, preserving every object and its references.
struct CallbackHook {
 SRWLOCK lock=SRWLOCK_INIT;
 std::atomic<void*> target{nullptr},original{nullptr};
 std::atomic<bool> enabled{false};
 bool Install(void* address,void* callback,const char* name) {
  if(!address)return false;
  if(enabled.load(std::memory_order_acquire))return target.load()==address;
  if(!TryAcquireSRWLockExclusive(&lock))return false;
  auto installed=target.load();MH_STATUS status=MH_OK;
  if(installed&&installed!=address){ReleaseSRWLockExclusive(&lock);return false;}
  if(!installed){
   void* trampoline=nullptr;status=MH_CreateHook(address,callback,&trampoline);
   if(status==MH_OK){original.store(trampoline,std::memory_order_release);target.store(address,std::memory_order_release);}
  }
  // MinHook suspends other threads. Never hold a callback lock while enabling.
  ReleaseSRWLockExclusive(&lock);
  if(status==MH_OK)status=MH_EnableHook(address);
  if(status==MH_OK||status==MH_ERROR_ENABLED){enabled.store(true,std::memory_order_release);return true;}
  char line[180];snprintf(line,sizeof(line),"metadata callback %s unavailable: %s",name,MH_StatusToString(status));QueueDiagnostic(line);
  return false;
 }
 template<class F> F Original()const{return reinterpret_cast<F>(original.load(std::memory_order_acquire));}
};
static CallbackHook display_getter,load_getter,console_callback,loading_callback,load_end_callback,load_start_callback;
struct Task {Base base;void(*execute)(Task*);};
struct PollTask {Task task;std::atomic<unsigned> refs{1};};
static Frame* polling_frame=nullptr;static SRWLOCK frame_lock=SRWLOCK_INIT;
static std::atomic<bool> poll_pending{false};static int(*post_task)(int,Task*)=nullptr;
static void PollAdd(Base* b){++reinterpret_cast<PollTask*>(b)->refs;}
static int PollDrop(Base* b){auto t=reinterpret_cast<PollTask*>(b);if(--t->refs)return 0;delete t;return 1;}
static int PollOne(Base* b){return reinterpret_cast<PollTask*>(b)->refs==1;}
static int PollAny(Base* b){return reinterpret_cast<PollTask*>(b)->refs>0;}
static std::wstring CollectorOptions() {
 return BuildCollectorOptions(GetSettings(),ReadAutoSkipRequest());
}
static std::wstring OnlineEnrichmentScript();
static std::wstring NativeArtworkScript();
static std::wstring MarkedPlaylistScript();
static void PollExecute(Task*) {
 Frame* frame=nullptr;
 {SharedSrw guard(frame_lock);frame=polling_frame;if(frame)frame->base.add(&frame->base);}
 struct Finish {Frame* frame;~Finish(){Release(frame);poll_pending=false;}} finish{frame};
 try {
  if(!frame || !reinterpret_cast<int(*)(Frame*)>(frame->methods[0])(frame))return;
  const auto artwork=NativeArtworkScript();
  const auto enrichment=OnlineEnrichmentScript();
  const auto code_text=CollectorOptions()+L"if(window.__floggfyPoll)window.__floggfyPoll();"+artwork+enrichment+MarkedPlaylistScript();
  String code={const_cast<wchar_t*>(code_text.c_str()),code_text.size(),nullptr};
  const wchar_t name[]=L"floggfy-metadata.js";String source={const_cast<wchar_t*>(name),wcslen(name),nullptr};
  reinterpret_cast<void(*)(Frame*,const String*,const String*,int)>(frame->methods[14])(frame,&code,&source,1);
 }catch(...) {/* A failed renderer poll must allow the next heartbeat. */}
}
static void SchedulePoll(){
 if(!post_task||poll_pending.exchange(true))return;
 auto t=new(std::nothrow) PollTask;if(!t){poll_pending=false;return;}
 t->task.base={sizeof(Task),PollAdd,PollDrop,PollOne,PollAny};t->task.execute=PollExecute;
 if(!post_task(0,&t->task))poll_pending=false; // The public CEF function consumes the task reference.
}
static std::shared_ptr<MetadataRecord> observed_metadata; // Current client snapshot, including legacy clients without a timeline.
static MetadataCache cache;static SRWLOCK cache_lock=SRWLOCK_INIT,message_lock=SRWLOCK_INIT;
static std::atomic<std::uint64_t> metadata_revision{1};
static hooks::InitController metadata_init;
static hooks::CallbackCounter metadata_callbacks;
static std::atomic<bool> metadata_running{false},metadata_polling{false};
static hooks::QueuedHookSet<3> metadata_startup_hooks;
static HANDLE metadata_preparation_thread=nullptr;
static bool metadata_prepared=false;
struct Message { ULONGLONG time=0; char data[131073];size_t length=0;};
static std::array<Message,4> messages;static size_t head=0,tail=0,count=0;static HANDLE message_event=nullptr;
static bool Enqueue(const String* value) {
 constexpr wchar_t prefix[]=L"FLOGGFY_METADATA_V1:";constexpr size_t n=sizeof(prefix)/sizeof(*prefix)-1;
 if(!value||value->length<=n||value->length>n+131072||wmemcmp(value->str,prefix,n))return false;
 if(TryAcquireSRWLockExclusive(&message_lock)) {
  if(count<messages.size()) {
   auto& m=messages[tail];m.time=GetTickCount64();m.length=value->length-n;bool valid=true;
   for(size_t i=0;i<m.length;i++){wchar_t c=value->str[n+i];if(c>127){valid=false;break;}m.data[i]=char(c);}
   if(valid){tail=(tail+1)%messages.size();++count;SetEvent(message_event);}
  } ReleaseSRWLockExclusive(&message_lock);
 } return true;
}
// One background lane resolves missing fields. Jobs pin the same record used
// by the matcher; no disk or network work runs on playback/decoder callbacks.
struct EnrichmentJob {std::shared_ptr<MetadataRecord> record;};
static SRWLOCK enrichment_lock=SRWLOCK_INIT;
static std::deque<EnrichmentJob> enrichment_jobs;
static HANDLE enrichment_event=nullptr,enrichment_thread=nullptr;
static std::atomic<bool> enrichment_running{false};
struct OnlineEnrichment {std::shared_ptr<MetadataRecord> owner;std::string uri;unsigned needs=0;};
static std::deque<OnlineEnrichment> online_enrichment; // Protected by cache_lock.
static bool HasField(const RichMetadata& metadata,const char* key) {
 const auto found=metadata.fields.find(key);return found!=metadata.fields.end()&&!found->second.empty();
}
static bool IsTrackUri(const std::string& uri) {
 return uri.size()==36&&uri.compare(0,14,"spotify:track:")==0&&
        std::all_of(uri.begin()+14,uri.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z');});
}
static bool NeedsStoredMetadata(const RichMetadata& metadata) {
 return IsTrackUri(metadata.uri)&&(!HasField(metadata,"DATE")||!HasField(metadata,"ISRC")||
        !HasField(metadata,"LABEL")||metadata.album_uri.empty());
}
static bool ReferencesArtwork(const RichMetadata& data,const std::string& reference) {
 return reference==data.artwork_url||
        std::find(data.artwork_urls.begin(),data.artwork_urls.end(),reference)!=data.artwork_urls.end();
}
static void MergeAvailableFields(const RichMetadata& available,RichMetadata& live) {
 if(available.uri!=live.uri)return;
 if(live.title.empty())live.title=available.title;
 if(live.artist.empty())live.artist=available.artist;
 if(live.album.empty())live.album=available.album;
 CachedTrackMetadata cached;
 cached.uri=available.uri;cached.title=available.title;cached.album=available.album;
 cached.album_uri=available.album_uri;
 cached.duration=available.duration;cached.fields=available.fields;
 MergeCachedMetadata(cached,live);
 // Optional omissions never erase an already identified cover. If Spotify
 // explicitly supplies a different reference, old bytes must not follow it.
 if(live.artwork_url.empty())live.artwork_url=available.artwork_url;
 if(live.artwork_urls.empty()&&live.artwork_url==available.artwork_url)live.artwork_urls=available.artwork_urls;
 if(live.metadata_source=="online"&&live.playback_quality.empty()&&!available.playback_quality.empty()){live.playback_quality=available.playback_quality;live.quality_time=available.quality_time;}
 if(!live.artwork&&available.artwork&&ReferencesArtwork(live,available.artwork->reference))live.artwork=available.artwork;
}
static void MergeSnapshot(const RichMetadata& previous,RichMetadata& incoming) {
 if(incoming.metadata_source=="online") {
  auto current=previous;current.metadata_source="online";
  MergeAvailableFields(incoming,current);incoming=std::move(current);
 }else MergeAvailableFields(previous,incoming);
}
static bool SameRecordingMetadata(const RichMetadata& left,const RichMetadata& right) {
 return left.title==right.title&&left.artist==right.artist&&left.album==right.album&&left.album_uri==right.album_uri&&
        left.uri==right.uri&&left.artwork_url==right.artwork_url&&left.artwork_urls==right.artwork_urls&&
        left.artwork==right.artwork&&left.duration==right.duration&&left.fields==right.fields;
}
static std::wstring JsString(const std::string& utf8) {
 const auto value=MetadataWide(utf8);std::wstring result=L"\"";
 for(const auto c:value) {
  if(c==L'\\'||c==L'"'){result+=L'\\';result+=c;}
  else if(c<32||c==0x2028||c==0x2029){wchar_t escaped[7];swprintf(escaped,7,L"\\u%04x",unsigned(c));result+=escaped;}
  else result+=c;
 }
 return result+L"\"";
}
static std::wstring MarkedPlaylistScript() {
 const auto settings=GetSettings();
 ConfigureMarkedCatalog(settings.root,settings.marked_when_downloaded);
 std::wstring script;
 MarkedQueryReply reply;
 if(settings.marked_when_downloaded&&TakeMarkedReply(reply)) {
  script=L"if(window.__floggfyMarkedReply)window.__floggfyMarkedReply({request:"+JsString(reply.request)+
      L",revision:"+std::to_wstring(reply.revision)+L",dates:[";
  bool first=true;
  for(const auto& item:reply.dates) {
   if(!first)script+=L",";
   first=false;
   script+=L"["+JsString(item.first)+L","+JsString(item.second)+L"]";
  }
  script+=L"]});";
 }
 script+=L"if(window.__floggfyMarkedPoll)window.__floggfyMarkedPoll(";
 script+=settings.marked_when_downloaded?L"true,":L"false,";
 return script+std::to_wstring(MarkedCatalogRevision())+L");";
}
// The enrichment worker owns one image request. CEF callbacks only copy its
// bounded reply; parsing, image decoding and publication stay on that worker.
struct NativeArtworkPending {
 std::string id,reply;
 std::wstring script;
 bool received=false,deferred=false;
};
static SRWLOCK native_artwork_lock=SRWLOCK_INIT;
static NativeArtworkPending native_artwork_pending;
static HANDLE native_artwork_event=nullptr;
static uint64_t next_native_artwork=0;
static std::wstring NativeArtworkScript() {
 ExclusiveSrw guard(native_artwork_lock);
 if(native_artwork_pending.script.empty())return {};
 const auto preferences=GetSettings();
 if(!preferences.metadata||!preferences.fetch_missing_artwork){
  native_artwork_pending.script.clear();
  native_artwork_pending.deferred=true;
  SetEvent(native_artwork_event);
  return {};
 }
 return std::exchange(native_artwork_pending.script,{});
}
static bool EnqueueNativeArtwork(const String* value) {
 constexpr wchar_t prefix[]=L"FLOGGFY_ARTWORK_V1:";
 constexpr size_t n=sizeof(prefix)/sizeof(*prefix)-1;
 if(!value||!value->str||value->length<n||wmemcmp(value->str,prefix,n))return false;
 // Even malformed reserved messages must never leak image bytes to logs.
 if(value->length<=n||value->length>n+kArtworkReplyMaxPayloadBytes)return true;
 try {
  std::string expected;
  {SharedSrw guard(native_artwork_lock);if(native_artwork_pending.id.empty()||native_artwork_pending.received)return true;expected="id="+native_artwork_pending.id+"&";}
  if(value->length-n<expected.size())return true;
  for(size_t i=0;i<expected.size();++i)if(value->str[n+i]!=wchar_t(expected[i]))return true;
  std::string reply;reply.reserve(value->length-n);
  for(size_t i=n;i<value->length;++i){if(value->str[i]>127)return true;reply+=char(value->str[i]);}
  {ExclusiveSrw guard(native_artwork_lock);
   if(native_artwork_pending.received||expected!="id="+native_artwork_pending.id+"&")return true;
   native_artwork_pending.reply=std::move(reply);native_artwork_pending.received=true;
   SetEvent(native_artwork_event);
  }
 }catch(...){QueueDiagnostic("metadata source=native-artwork status=unavailable reason=reply-allocation-failed");}
 return true;
}
static bool ApplyNativeArtworkReply(RichMetadata& data,ArtworkReply& reply,const std::string& id,std::string& reason) {
 if(reply.id!=id||reply.uri!=data.uri){reason="reply-identity-mismatch";return false;}
 if(reply.status!="ready"){reason=reply.reason;return false;}
 if(!IsAlbumArtworkUrl(reply.reference)||!ReferencesArtwork(data,reply.reference)){
  reason="reply-reference-mismatch";return false;
 }
 auto bytes=std::move(reply.bytes);ImageInfo info;
 if(!NormalizeArtworkImage(bytes,reason)||!ReadImageInfo(bytes,info))return false;
 auto image=std::make_shared<MetadataArtwork>();image->reference=reply.reference;
 image->mime=info.mime;image->bytes=std::move(bytes);data.artwork=std::move(image);
 reason="validated";return true;
}
// Return false when dispatch was deferred by a live setting change.
static bool AttemptNativeArtwork(RichMetadata& data) {
 {SharedSrw guard(frame_lock);if(!polling_frame)return true;}
 const auto& references=data.artwork_urls;
 if(!native_artwork_event)native_artwork_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 if(!native_artwork_event)return true;
 const auto id=std::to_string(++next_native_artwork);
 const auto started=GetTickCount64();
 std::wstring urls=L"[";for(const auto& url:references){if(urls.size()>1)urls+=L",";urls+=JsString(url);}urls+=L"]";
 const auto unavailable="id="+id+"&uri="+data.uri+"&source=0&status=unavailable&reason=collector-unavailable";
 auto script=L"if(window.__floggfyArtwork)window.__floggfyArtwork("+JsString(id)+L","+JsString(data.uri)+L","+urls+
  L");else console.info("+JsString("FLOGGFY_ARTWORK_V1:"+unavailable)+L");";
 ResetEvent(native_artwork_event);
 {ExclusiveSrw guard(native_artwork_lock);
  const auto preferences=GetSettings();
  if(!preferences.metadata||!preferences.fetch_missing_artwork)return false;
  native_artwork_pending={id,{},std::move(script)};
 }
 if(message_event)SetEvent(message_event);
 char line[384];snprintf(line,sizeof(line),"metadata uri=%s source=native-artwork status=requested request=%s candidates=%zu",data.uri.c_str(),id.c_str(),references.size());QueueDiagnostic(line);
 const auto wait=WaitForSingleObject(native_artwork_event,8500);
 std::string response;
 {ExclusiveSrw guard(native_artwork_lock);
  const bool deferred=native_artwork_pending.deferred;
  response=std::move(native_artwork_pending.reply);native_artwork_pending={};
  if(deferred)return false;
 }
 ArtworkReply reply;std::string reason=wait==WAIT_TIMEOUT?"timeout":"reply-unavailable";
 bool ready=false;
 if(!GetSettings().metadata)reason="metadata-disabled";
 else if(!response.empty()&&ParseArtworkReply(response,reply,reason)) {
  std::string().swap(response); // Release the encoded envelope before image conversion.
  ready=ApplyNativeArtworkReply(data,reply,id,reason);
 }
 const char* origin=reply.source==1?"local-file":reply.source==2?"cache":reply.source==3?"network":"unknown";
 snprintf(line,sizeof(line),"metadata uri=%s source=native-artwork status=%s request=%s origin=%s image_id=%s reason=%s bytes=%zu elapsed_ms=%llu",data.uri.c_str(),
     ready?"ready":"unavailable",id.c_str(),origin,reply.reference.empty()?"none":reply.reference.c_str()+24,reason.c_str(),ready?data.artwork->bytes.size():0,GetTickCount64()-started);QueueDiagnostic(line);
 return reason!="disabled";
}
static unsigned OnlineNeeds(const RichMetadata& data,bool lyrics) {
 if(!IsTrackUri(data.uri))return 0;
 const bool album=!HasField(data,"LABEL")||data.artwork_url.empty();
 return ((!HasField(data,"DATE")||!HasField(data,"ISRC")||(album&&!ValidSpotifyAlbumUri(data.album_uri)))?1u:0u)|
        (album?2u:0u)|
        ((lyrics&&!HasField(data,"LYRICS"))?4u:0u);
}
static unsigned AllowedOnlineNeeds(const Settings& preferences) {
 if(!preferences.metadata)return 0;
 return (preferences.fetch_missing_metadata?3u:0u)|
        (preferences.lyrics&&preferences.fetch_missing_lyrics?4u:0u);
}
static std::wstring OnlineEnrichmentScript() {
 RichMetadata m;
 unsigned needs=0;
 {ExclusiveSrw guard(cache_lock);
  if(online_enrichment.empty())return {};
  const auto& request=online_enrichment.front();
  const auto& live=request.owner->value;
  const auto preferences=GetSettings();
  if(live.uri==request.uri)needs=request.needs&OnlineNeeds(live,preferences.lyrics)&AllowedOnlineNeeds(preferences);
  if(needs){
   m.uri=live.uri;m.title=live.title;m.artist=live.artist;m.album=live.album;m.duration=live.duration;
   m.album_uri=live.album_uri;m.artwork_url=live.artwork_url;m.artwork_urls=live.artwork_urls;
  }
  request.owner->online_requested&=~(request.needs&~needs);
  online_enrichment.pop_front();
 }
 if(!needs)return {};
 std::string artwork_urls;for(const auto& url:m.artwork_urls){if(!artwork_urls.empty())artwork_urls+='\n';artwork_urls+=url;}
 const auto truth=[](bool value){return value?L"true":L"false";};
 // Only one request grant per UI tick; the collector owns asynchronous sends.
 return L"if(window.__floggfyEnrich)window.__floggfyEnrich("+JsString(m.uri)+
  L",{metadata:"+truth(needs&1)+L",album:"+truth(needs&2)+L",lyrics:"+truth(needs&4)+
  L"},{uri:"+JsString(m.uri)+L",title:"+JsString(m.title)+L",artist:"+JsString(m.artist)+
  L",album:"+JsString(m.album)+L",duration:"+std::to_wstring(m.duration)+L",album_uri:"+JsString(m.album_uri)+
  L",artwork_url:"+JsString(m.artwork_url)+L",artwork_urls:"+JsString(artwork_urls)+L",SPOTIFY_URI:"+JsString(m.uri)+L"});";
}
static void QueueOnlineEnrichment(const std::shared_ptr<MetadataRecord>& owner) {
 ExclusiveSrw guard(cache_lock);
 const auto& data=owner->value;
 const auto preferences=GetSettings();
 const unsigned needs=OnlineNeeds(data,preferences.lyrics)&AllowedOnlineNeeds(preferences)&~owner->online_requested;
 if(!needs)return;
 for(auto it=online_enrichment.begin();it!=online_enrichment.end();)
  if(it->owner.use_count()==2){it->owner->online_requested&=~it->needs;it=online_enrichment.erase(it);}else ++it;
 online_enrichment.push_back({owner,data.uri,needs});owner->online_requested|=needs;
 char line[320];snprintf(line,sizeof(line),"metadata uri=%s source=online status=requested reason=missing-after-local date=%u isrc=%u label=%u artwork=%u lyrics=%u",data.uri.c_str(),
     unsigned(!HasField(data,"DATE")),unsigned(!HasField(data,"ISRC")),unsigned(!HasField(data,"LABEL")),unsigned(data.artwork_url.empty()),unsigned((needs&4)!=0));QueueDiagnostic(line);
}
static void ResolveArtwork(RichMetadata& data,bool online) {
 const auto& references=data.artwork_urls;
 size_t attempt=0;
 for(const auto& url:references) {
  ++attempt;
  std::string reason=online?"download-failed":"cache-miss";
  std::vector<uint8_t> bytes;ImageInfo info;
  const bool found=online?DownloadAlbumArtwork(url,bytes):ReadCachedArtwork(url,bytes,reason);
  const bool valid=found&&ReadImageInfo(bytes,info);
  if(found)reason=valid?"validated":"image-invalid";
  // Keep every candidate's outcome: a later miss must not hide an earlier
  // cached image that failed conversion. Image IDs contain no request secrets.
  char line[384];snprintf(line,sizeof(line),"metadata uri=%s source=%s status=%s candidate=%zu/%zu image_id=%s reason=%s bytes=%zu width=%u height=%u",data.uri.c_str(),
      online?"online-artwork":"local-artwork",valid?"ready":"unavailable",attempt,references.size(),url.c_str()+24,reason.c_str(),bytes.size(),info.width,info.height);QueueDiagnostic(line);
  if(valid) {
   auto image=std::make_shared<MetadataArtwork>();image->reference=url;image->mime=info.mime;image->bytes=std::move(bytes);data.artwork=std::move(image);
   return;
  }
 }
}
static void PublishEnrichment(const std::shared_ptr<MetadataRecord>& owner,const RichMetadata& data) {
 const bool lyrics=GetSettings().lyrics;
 ExclusiveSrw guard(cache_lock);
 auto merged=owner->value;MergeAvailableFields(data,merged);
 if(!lyrics)merged.fields.erase("LYRICS");
 const bool changed=!SameRecordingMetadata(owner->value,merged);
 owner->value=std::move(merged);
 if(changed)metadata_revision.fetch_add(1,std::memory_order_release);
}
static std::vector<std::string> ArtworkCandidates(const RichMetadata& data) {
 std::vector<std::string> candidates;
 for(const auto& url:data.artwork_urls) {
  if(IsAlbumArtworkUrl(url)&&std::find(candidates.begin(),candidates.end(),url)==candidates.end())candidates.push_back(url);
  if(candidates.size()==8)break;
 }
 if(candidates.empty()&&IsAlbumArtworkUrl(data.artwork_url))candidates.push_back(data.artwork_url);
 return candidates;
}
static bool ArtworkCandidatesChanged(const std::vector<std::string>& candidates,const std::vector<std::string>& attempted) {
 return !std::is_permutation(candidates.begin(),candidates.end(),attempted.begin(),attempted.end());
}
enum class ArtworkStage { Native,Cached,Online };
static RichMetadata BeginArtwork(const std::shared_ptr<MetadataRecord>& owner,ArtworkStage stage) {
 ExclusiveSrw guard(cache_lock);
 const auto preferences=GetSettings();
 const auto& live=owner->value;
 if(!preferences.metadata||live.artwork||live.artwork_url.empty()||
    (stage!=ArtworkStage::Cached&&!preferences.fetch_missing_artwork))return {};
 auto candidates=ArtworkCandidates(live);
 const bool cached_pending=ArtworkCandidatesChanged(candidates,owner->cached_artwork_attempted);
 const bool native_pending=ArtworkCandidatesChanged(candidates,owner->native_artwork_attempted);
 if(stage==ArtworkStage::Online) {
  // A replacement reference may have arrived during a local lookup.
  if(cached_pending||native_pending||!IsAlbumArtworkUrl(live.artwork_url)||owner->online_artwork_attempted==live.artwork_url)return {};
  candidates={live.artwork_url};
 }else if(stage==ArtworkStage::Cached?!cached_pending:!native_pending)return {};
 RichMetadata work;
 work.uri=live.uri;work.artwork_url=live.artwork_url;work.artwork_urls=std::move(candidates);
 if(stage==ArtworkStage::Cached)owner->cached_artwork_attempted=work.artwork_urls;
 if(stage==ArtworkStage::Online)owner->online_artwork_attempted=work.artwork_url;
 return work;
}
static void PublishArtwork(const std::shared_ptr<MetadataRecord>& owner,const RichMetadata& work) {
 if(!work.artwork)return;
 ExclusiveSrw guard(cache_lock);
 auto& live=owner->value;
 if(live.uri!=work.uri||live.artwork||!ReferencesArtwork(live,work.artwork->reference))return;
 live.artwork=work.artwork;
 metadata_revision.fetch_add(1,std::memory_order_release);
}
static void QueueEnrichment(const std::shared_ptr<MetadataRecord>& record);
static DWORD WINAPI EnrichmentWorker(LPVOID) {
 SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
 while(enrichment_running.load(std::memory_order_acquire)) {
  EnrichmentJob job;
  {ExclusiveSrw guard(enrichment_lock);if(!enrichment_jobs.empty()){job=std::move(enrichment_jobs.front());enrichment_jobs.pop_front();}}
  if(!job.record){WaitForSingleObject(enrichment_event,1000);continue;}
  {ExclusiveSrw guard(cache_lock);
   // Registry + queued job only: playback/matcher/save no longer needs it.
   if(job.record.use_count()==2){job.record->queued=false;continue;}
  }
  auto owner=job.record;
  bool processed=false;
  try {
   RichMetadata local;bool read_local=false;
   {ExclusiveSrw guard(cache_lock);
    const auto preferences=GetSettings();
    read_local=preferences.metadata&&preferences.read_spotify_disk_cache&&!owner->local_attempted&&NeedsStoredMetadata(owner->value);
    if(read_local){local=owner->value;owner->local_attempted=true;}
   }
   if(read_local) {
    try{EnrichStoredMetadata(local);}catch(...){QueueDiagnostic("metadata source=local-cache status=failed reason=exception");}
    PublishEnrichment(owner,local); // Local tags are ready even while artwork is pending.
   }
   auto artwork=BeginArtwork(owner,ArtworkStage::Native);
   if(!artwork.artwork_urls.empty()) {
    if(AttemptNativeArtwork(artwork)){
     ExclusiveSrw guard(cache_lock);
     owner->native_artwork_attempted=std::move(artwork.artwork_urls);
    }
    PublishArtwork(owner,artwork);
   }
   artwork=BeginArtwork(owner,ArtworkStage::Cached);
   if(!artwork.artwork_urls.empty()){
    ResolveArtwork(artwork,false);
    PublishArtwork(owner,artwork);
   }
   QueueOnlineEnrichment(owner);
   artwork=BeginArtwork(owner,ArtworkStage::Online);
   if(!artwork.artwork_urls.empty()){
    ResolveArtwork(artwork,true);
    PublishArtwork(owner,artwork);
   }
   processed=true;
  }catch(...) {QueueDiagnostic("metadata enrichment failed; current client metadata retained");}
  {ExclusiveSrw guard(cache_lock);owner->queued=false;}
  // A renderer update or a live enable can arrive while this owner is queued.
  if(processed)try{QueueEnrichment(owner);}catch(...){QueueDiagnostic("metadata fallback requeue failed; next client snapshot will retry");}
 }
 return 0;
}
static bool StartEnrichmentWorker() {
 // Initialization and all calls originate on the one poll/message worker.
 // Resources live for the process, including conservative hook rollback paths.
 if(enrichment_thread)return true;
 enrichment_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 if(!enrichment_event)return false;
 enrichment_running=true;
 enrichment_thread=CreateThread(nullptr,0,EnrichmentWorker,nullptr,0,nullptr);
 if(!enrichment_thread){enrichment_running=false;CloseHandle(enrichment_event);enrichment_event=nullptr;return false;}
 return true;
}
static void QueueEnrichment(const std::shared_ptr<MetadataRecord>& record) {
 if(!enrichment_thread)return;
 {ExclusiveSrw guard(cache_lock);
  if(record->queued)return;
  const auto& data=record->value;
  const auto preferences=GetSettings();if(!preferences.metadata)return;
  if(!preferences.lyrics)record->online_requested&=~4u;
  const bool missing_local=preferences.read_spotify_disk_cache&&!record->local_attempted&&NeedsStoredMetadata(data);
  const bool missing_online=(OnlineNeeds(data,preferences.lyrics)&AllowedOnlineNeeds(preferences)&~record->online_requested)!=0;
  bool missing_artwork=false;
  if(!data.artwork&&!data.artwork_url.empty()){
   const auto candidates=ArtworkCandidates(data);
   missing_artwork=ArtworkCandidatesChanged(candidates,record->cached_artwork_attempted)||
    (preferences.fetch_missing_artwork&&(ArtworkCandidatesChanged(candidates,record->native_artwork_attempted)||
      (IsAlbumArtworkUrl(data.artwork_url)&&record->online_artwork_attempted!=data.artwork_url)));
  }
  if(!missing_local&&!missing_online&&!missing_artwork)return;
  record->queued=true;
 }
 try {
  ExclusiveSrw records_guard(cache_lock);ExclusiveSrw jobs_guard(enrichment_lock);
  for(auto it=enrichment_jobs.begin();it!=enrichment_jobs.end();)
   if(it->record.use_count()==2){it->record->queued=false;it=enrichment_jobs.erase(it);}else ++it;
  enrichment_jobs.push_back({record});
 }
 catch(...){ExclusiveSrw guard(cache_lock);record->queued=false;throw;}
 SetEvent(enrichment_event);
}
static DWORD WINAPI MetadataWorker(LPVOID) {
 SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
 StartEnrichmentWorker();
 Message message;
 auto enable_revision=EnrichmentEnableRevision();
 while(metadata_running.load(std::memory_order_acquire)) {
  const auto current_revision=EnrichmentEnableRevision();
  const bool fallback_enabled=current_revision!=enable_revision;
  enable_revision=current_revision;
  if(fallback_enabled)try {
   std::vector<std::shared_ptr<MetadataRecord>> pending;
   {SharedSrw guard(cache_lock);pending=cache.Pinned();}
   for(const auto& owner:pending)QueueEnrichment(owner);
  }catch(...) {QueueDiagnostic("metadata fallback refresh failed; next client snapshot will retry");}
  if(metadata_polling.load(std::memory_order_acquire))SchedulePoll();
  bool got=false;
  {ExclusiveSrw guard(message_lock);
   if(count){message=messages[head];head=(head+1)%messages.size();--count;got=true;}}
  if(!got){WaitForSingleObject(message_event,500);continue;}
  try {
   const auto preferences=GetSettings();if(!preferences.metadata)continue;
   RichMetadata metadata;std::string error;
   if(!ParseRichMetadata(std::string(message.data,message.length),metadata,error))continue;
   if(!metadata.playback_quality.empty())metadata.quality_time=message.time;
   std::shared_ptr<MetadataRecord> owner;
   {ExclusiveSrw guard(cache_lock);
    const auto previous=metadata.uri.empty()?cache.Find(metadata.title,metadata.artist,metadata.album,metadata.duration):cache.FindUri(metadata.uri);
    if(previous && previous->uri==metadata.uri)MergeSnapshot(*previous,metadata);
    if(!preferences.lyrics)metadata.fields.erase("LYRICS");
    const bool changed=!previous||!SameRecordingMetadata(*previous,metadata);
    owner=cache.Pin(metadata);owner->value=metadata;
    if(metadata.metadata_source!="online")observed_metadata=owner;
    if(changed)metadata_revision.fetch_add(1,std::memory_order_release);}
   QueueEnrichment(owner);
  }catch(...) {QueueDiagnostic("metadata message failed; next client snapshot will retry");}
 }
 return 0;
}
static bool CurrentTimeline(const String* text) {
 constexpr wchar_t prefix[]=L"FLOGGFY_TIMELINE_V1:";
 constexpr size_t n=sizeof(prefix)/sizeof(*prefix)-1;
 if(!text||!text->str||text->length<n||wmemcmp(text->str,prefix,n))return false;
 try {
  if(text->length>n+16384){PublishClientMediaUnavailable("client-payload-too-large");return true;}
  std::string payload;payload.reserve(text->length-n);
  for(size_t i=n;i<text->length;++i){if(text->str[i]>127){PublishClientMediaUnavailable("client-invalid-encoding");return true;}payload+=char(text->str[i]);}
  if(payload=="v=1&blocked=buffering"||payload.rfind("v=1&blocked=buffering&",0)==0){
   ClientTransition transition;
   if(ParseClientTransition(payload,transition)) {
    Media next;next.source=MediaSource::Spotify;next.spotify_uri=transition.uri;next.playback_instance=transition.instance;next.position=transition.position;next.explicit_skip=transition.explicit_skip;next.explicit_control=transition.explicit_control;next.control_monitoring=transition.control_monitoring;next.natural_transition=transition.natural;next.transition_pending=transition.pending;next.terminal_context=transition.terminal;
    PublishClientBufferingTransition(std::move(next),transition.sampled_ms,transition.transition_ms);
   }else PublishClientMediaUnavailable("client-buffering",true);
   return true;
  }
  if(payload=="v=1&blocked=remote-playback"||payload.rfind("v=1&blocked=remote-playback&",0)==0){PublishClientMediaUnavailable("remote-playback",true);return true;}
  ClientInterstitialKind interstitial=ClientInterstitialKind::None;
  if(ParseClientActiveWithoutUri(payload,&interstitial)) {
   PublishClientMediaUnavailable(interstitial==ClientInterstitialKind::Advertisement?
    "client-active-advertisement":"client-active-interstitial",true);return true;
  }
  ClientPlaybackRecord record;std::string error;
  if(!ParseClientPlayback(payload,record,error)){PublishClientMediaUnavailable("client-timeline-unavailable");return true;}
  if(record.local==0){PublishClientMediaUnavailable("remote-playback",true);return true;}
  if(record.buffering){PublishClientMediaUnavailable("client-buffering",true);return true;}
  if(record.local<0){PublishClientMediaUnavailable("client-device-unknown");return true;}
  Media media;media.source=MediaSource::Spotify;media.spotify_uri=record.metadata.uri;media.playback_instance=record.instance;
  media.title=MetadataWide(record.metadata.title);media.artist=MetadataWide(record.metadata.artist);media.album=MetadataWide(record.metadata.album);
  media.position=record.position;media.duration=record.metadata.duration;media.playback_speed=record.speed;media.playing=record.playing;media.playback_status=record.playing?4:5;
  media.explicit_control=record.explicit_control;
  media.prior_control=record.prior_control;
  {ExclusiveSrw guard(cache_lock);media.metadata=cache.Pin(record.metadata);}
  PublishClientMedia(std::move(media),record.sampled_ms);
 }catch(...){InvalidateClientMedia();try{PublishClientMediaUnavailable("client-timeline-exception");}catch(...){}}
 return true;
}
static bool CurrentPlayback(const String* text) {
 constexpr wchar_t prefix[]=L"FLOGGFY_PLAYBACK_V1:";
 constexpr size_t n=sizeof(prefix)/sizeof(*prefix)-1;
 if(!text || !text->str || text->length<n || wmemcmp(text->str,prefix,n))return false;
 // Bounded cached-state message only: no file reads, enrichment or API calls.
 try {
  PlaybackQualitySnapshot snapshot;
  Media item;
  if(text->length<=n+4096) {
   std::string payload;payload.reserve(text->length-n);
   for(size_t i=n;i<text->length;++i) {
    if(text->str[i]>127){PublishCurrentItem({});PublishClientPlaybackQuality({});return true;}
    payload+=char(text->str[i]);
   }
   RichMetadata record;std::string error;
   if(ParsePlaybackMetadata(payload,record,error)) {
    item.source=MediaSource::Spotify;item.spotify_uri=record.uri;
    item.title=MetadataWide(record.title);item.artist=MetadataWide(record.artist);item.album=MetadataWide(record.album);
    item.duration=record.duration;
    snapshot.identity=PlaybackIdentity(record.uri.empty()
        ? record.title+"\x1f"+record.artist+"\x1f"+record.album : record.uri);
    snapshot.level=ParsePlaybackLevel(record.playback_quality);
    const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,record.title.data(),int(record.title.size()),nullptr,0);
    if(length>0 && length<=4096) {
     std::wstring title(size_t(length),L'\0');
     MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,record.title.data(),int(record.title.size()),title.data(),length);
     auto count=std::min<size_t>(title.size(),snapshot.title.size()-1);
     if(count && title[count-1]>=0xd800 && title[count-1]<=0xdbff)--count;
     std::copy_n(title.begin(),count,snapshot.title.begin());
    }
   }
  }
  PublishCurrentItem(std::move(item));
  PublishClientPlaybackQuality(snapshot);
 }catch(...){PublishCurrentItem({});PublishClientPlaybackQuality({});}
 return true;
}
static int Console(Display* self,Browser* b,int level,const String* text,const String* source,int line) {
 auto callback=metadata_callbacks.Enter();
 constexpr std::wstring_view marked_prefix=L"FLOGGFY_MARKED_V1:";
 if(text&&text->str&&text->length>=marked_prefix.size()&&
    !wmemcmp(text->str,marked_prefix.data(),marked_prefix.size())) {
  try {
   std::string request;std::vector<std::string> uris;
   if(ParseMarkedQuery({text->str,text->length},request,uris))QueueMarkedQuery(request,uris,SchedulePoll);
  }catch(...) {/* Invalid or allocation-limited queries cannot escape CEF. */}
  Release(b);return 1;
 }
 constexpr wchar_t privacy_prefix[]=L"FLOGGFY_PRIVACY_V1:";
 constexpr size_t privacy_prefix_size=sizeof(privacy_prefix)/sizeof(*privacy_prefix)-1;
 if(text&&text->str&&text->length>=privacy_prefix_size&&
    !wmemcmp(text->str,privacy_prefix,privacy_prefix_size)){
  privacy::ObserveRenderer({text->str+privacy_prefix_size,text->length-privacy_prefix_size});
  Release(b);return 1;
 }
 constexpr wchar_t capability_prefix[]=L"FLOGGFY_PRIVACY_V2:";
 constexpr size_t capability_prefix_size=sizeof(capability_prefix)/sizeof(*capability_prefix)-1;
 if(text&&text->str&&text->length>=capability_prefix_size&&
    !wmemcmp(text->str,capability_prefix,capability_prefix_size)){
  privacy::ObserveRendererCapabilities({text->str+capability_prefix_size,text->length-capability_prefix_size});
  Release(b);return 1;
 }
 constexpr wchar_t count_prefix[]=L"FLOGGFY_PRIVACY_COUNT_V1:";
 constexpr size_t count_prefix_size=sizeof(count_prefix)/sizeof(*count_prefix)-1;
 if(text&&text->str&&text->length>=count_prefix_size&&
    !wmemcmp(text->str,count_prefix,count_prefix_size)){
  privacy::ObserveRendererCount({text->str+count_prefix_size,text->length-count_prefix_size});
  Release(b);return 1;
 }
 constexpr wchar_t dump_prefix[]=L"FLOGGFY_PRIVACY_DUMP_V1:";
 constexpr size_t dump_prefix_size=sizeof(dump_prefix)/sizeof(*dump_prefix)-1;
 if(text&&text->str&&text->length>=dump_prefix_size&&
    !wmemcmp(text->str,dump_prefix,dump_prefix_size)){
  privacy::ObserveRendererDump({text->str+dump_prefix_size,text->length-dump_prefix_size});
  Release(b);return 1; // Payloads never fall through to ordinary console logs.
 }
 if(EnqueueNativeArtwork(text)||CurrentTimeline(text)||CurrentPlayback(text)){Release(b);return 1;}
 if(text&&text->str&&QueueRendererDiagnostic({text->str,text->length})) {
  Release(b);return 1; // Reserved diagnostics never fall through to raw console logging.
 }
 if(Enqueue(text)){Release(b);return 1;}
 return console_callback.Original<int(*)(Display*,Browser*,int,const String*,const String*,int)>()(self,b,level,text,source,line);
}
static void InjectPrivacy(Frame* frame) {
 if(!privacy::Enabled()||!frame||frame->base.size!=sizeof(Frame))return;
 const auto main=reinterpret_cast<int(*)(Frame*)>(frame->methods[15]);
 const auto execute=reinterpret_cast<void(*)(Frame*,const String*,const String*,int)>(frame->methods[14]);
 if(!main||!execute||!main(frame))return;
 const auto settings=GetSettings();
 // Pass every switch explicitly: omitted or false options never opt in. Keep
 // the constant local to each injection so repeated frame injection is valid.
 std::wstring script=L"(()=>{const __floggfyPrivacyOptions=Object.freeze({";
 const auto option=[&script](const wchar_t* key,bool enabled){script+=key;script+=enabled?L":true,":L":false,";};
 option(L"block_telemetry",settings.block_telemetry);
 option(L"block_event_reports",settings.block_event_reports);
 option(L"block_playback_reports",settings.block_playback_reports);
 option(L"block_growth_analytics",settings.block_growth_analytics);
 option(L"block_sentry",settings.block_sentry);
 option(L"block_crash_reports",settings.block_crash_reports);
 option(L"block_internal_logging",settings.block_internal_logging);
 option(L"block_track_errors",settings.block_track_errors);
 option(L"block_ai_telemetry",settings.block_ai_telemetry);
 option(L"block_queued_telemetry",settings.block_queued_telemetry);
 option(L"dump_telemetry",settings.dump_telemetry);
 script+=L"});";script+=telemetry_script;script+=L"})();";
 String code={script.data(),script.size(),nullptr};
 const wchar_t name[]=L"floggfy-privacy.js";String source={const_cast<wchar_t*>(name),wcslen(name),nullptr};
 execute(frame,&code,&source,1);
}
static void LoadStart(Load* self,Browser* b,Frame* f,int transition){
 auto callback=metadata_callbacks.Enter();
 try{InjectPrivacy(f);}catch(...) {/* Privacy failure must not interrupt loading. */}
 load_start_callback.Original<void(*)(Load*,Browser*,Frame*,int)>()(self,b,f,transition);
}
static void Inject(Frame* frame) {
 InjectPrivacy(frame);
 if(!frame||frame->base.size!=sizeof(Frame))return;
 auto main=reinterpret_cast<int(*)(Frame*)>(frame->methods[15]);
 if(!main||!main(frame))return;
 frame->base.add(&frame->base);Frame* old=nullptr;
 {ExclusiveSrw guard(frame_lock);old=polling_frame;polling_frame=frame;}Release(old);
 const auto code_text=CollectorOptions()+metadata_script+MarkedPlaylistScript();
 String code={const_cast<wchar_t*>(code_text.c_str()),code_text.size(),nullptr};
 const wchar_t name[]=L"floggfy-metadata.js";String source={const_cast<wchar_t*>(name),wcslen(name),nullptr};
 reinterpret_cast<void(*)(Frame*,const String*,const String*,int)>(frame->methods[14])(frame,&code,&source,1);
}
static void Loading(Load* self,Browser* b,int loading,int back,int forward){
 auto callback=metadata_callbacks.Enter();
 if(!loading&&b&&b->base.size==sizeof(Browser)){auto frame=reinterpret_cast<Frame*(*)(Browser*)>(b->methods[14])(b);Inject(frame);Release(frame);}
 loading_callback.Original<void(*)(Load*,Browser*,int,int,int)>()(self,b,loading,back,forward);
}
static void LoadEnd(Load* self,Browser* b,Frame* f,int status){
 auto callback=metadata_callbacks.Enter();Inject(f);
 load_end_callback.Original<void(*)(Load*,Browser*,Frame*,int)>()(self,b,f,status);
}
static Display* GetDisplay(Client* self){
 auto callback=metadata_callbacks.Enter();auto result=display_getter.Original<Display*(*)(Client*)>()(self);
 if(result&&result->base.size==sizeof(Display))console_callback.Install(result->methods[6],reinterpret_cast<void*>(Console),"console");
 return result;
}
static Load* GetLoad(Client* self){
 auto callback=metadata_callbacks.Enter();auto result=load_getter.Original<Load*(*)(Client*)>()(self);
 if(result&&result->base.size==sizeof(Load)){
  if(privacy::Enabled())load_start_callback.Install(result->methods[1],reinterpret_cast<void*>(LoadStart),"privacy load start");
  loading_callback.Install(result->methods[0],reinterpret_cast<void*>(Loading),"loading");
  load_end_callback.Install(result->methods[2],reinterpret_cast<void*>(LoadEnd),"load end");
 }
 return result;
}
static Client* ObserveClient(Client* client){
 privacy::ObserveClient(client);
 if(!client||client->base.size!=sizeof(Client)){LogEvent(LogKind::Warning,"warning","metadata client ABI unsupported; collector not attached");return client;}
 const bool display=display_getter.Install(client->methods[4],reinterpret_cast<void*>(GetDisplay),"display getter");
 const bool load=load_getter.Install(client->methods[14],reinterpret_cast<void*>(GetLoad),"load getter");
 if(!display||!load)LogEvent(LogKind::Warning,"warning","Spotify app observer callbacks unavailable; Windows media fallback may be required");
 return client;
}
using Create=int(*)(const void*,Client*,const String*,const void*,void*,void*);
using Sync=Browser*(*)(const void*,Client*,const String*,const void*,void*,void*);
using View=void*(*)(Client*,const String*,const void*,void*,void*,void*);
static Create original_create;static Sync original_sync;static View original_view;
static int CreateHook(const void* win,Client* client,const String* url,const void* settings,void* extra,void* context){auto callback=metadata_callbacks.Enter();return original_create(win,ObserveClient(client),url,settings,extra,context);}
static Browser* SyncHook(const void* win,Client* client,const String* url,const void* settings,void* extra,void* context){auto callback=metadata_callbacks.Enter();return original_sync(win,ObserveClient(client),url,settings,extra,context);}
static void* ViewHook(Client* client,const String* url,const void* settings,void* extra,void* context,void* delegate){auto callback=metadata_callbacks.Enter();return original_view(ObserveClient(client),url,settings,extra,context,delegate);}
}
std::uint64_t MetadataRevision() {
 // Both counters are monotonic. Artwork freshness becomes visible while its
 // bytes are committed, before this worker reacquires the metadata cache lock.
 return metadata_revision.load(std::memory_order_acquire)+AlbumArtworkRevision();
}
void RetainMetadata(Media& media) {
 if(media.metadata)return;
 ExclusiveSrw guard(cache_lock);
 const auto existing=media.spotify_uri.empty()?cache.Find(Utf8(media.title),Utf8(media.artist),Utf8(media.album),media.duration):cache.FindUri(media.spotify_uri);
 if(existing){media.metadata=cache.Pin(*existing);return;}
 if(media.spotify_uri.empty())return;
 RichMetadata basic;basic.uri=media.spotify_uri;basic.title=Utf8(media.title);basic.artist=Utf8(media.artist);basic.album=Utf8(media.album);basic.duration=media.duration;
 media.metadata=cache.Pin(basic);
}
static bool ReadCachedMetadata(const Media& media,RichMetadata& metadata) {
 const auto title=Utf8(media.title),artist=Utf8(media.artist),album=Utf8(media.album);
 SharedSrw guard(cache_lock);
 const auto record=media.metadata&&media.metadata->value.uri==media.spotify_uri?&media.metadata->value:
     media.spotify_uri.empty()?cache.Find(title,artist,album,media.duration):cache.FindUri(media.spotify_uri);
 if(!record)return false;
 metadata=*record;
 return true;
}
std::string ReadClientPlaybackQuality(const Media& media) {
 if(!GetSettings().metadata)return {};
 const auto title=Utf8(media.title),artist=Utf8(media.artist),album=Utf8(media.album);
 SharedSrw guard(cache_lock);
 const auto record=media.metadata&&media.metadata->value.uri==media.spotify_uri?&media.metadata->value:
     media.spotify_uri.empty()?cache.Find(title,artist,album,media.duration):cache.FindUri(media.spotify_uri);
 const auto now=GetTickCount64();
 return record && record->quality_time && now>=record->quality_time && now-record->quality_time<=20000 ? record->playback_quality : std::string{};
}
static unsigned MetadataNumber(const std::string& text) {
 if(text.empty()||text.size()>6)return 0;
 unsigned number=0;
 for(char digit:text){if(digit<'0'||digit>'9')return 0;number=number*10+unsigned(digit-'0');}
 return number;
}
void EnrichCatalog(const Media& media,Catalog& catalog) {
 if(!GetSettings().metadata)return;
 const auto title=Utf8(media.title),artist=Utf8(media.artist),album=Utf8(media.album);
 const char* keys[]={"TITLE","ALBUM","ALBUMARTIST","ARTIST","TRACKNUMBER","DISCNUMBER"};
 std::array<std::string,6> fields;std::string uri,album_uri;
 {SharedSrw guard(cache_lock);
  const auto record=media.metadata&&media.metadata->value.uri==media.spotify_uri?&media.metadata->value:
     media.spotify_uri.empty()?cache.Find(title,artist,album,media.duration):cache.FindUri(media.spotify_uri);
  if(!record)return;
  uri=record->uri;
  album_uri=record->album_uri;
  for(size_t i=0;i<fields.size();++i){const auto value=record->fields.find(keys[i]);if(value!=record->fields.end())fields[i]=value->second;}}
 // Decode and allocate the complete replacement outside the cache lock. Keep
 // publication tags and the Song probe on the same canonical directory identity.
 auto next=catalog;next.spotify_uri=std::move(uri);next.spotify_album_uri=std::move(album_uri);
 auto replace=[](std::wstring& target,const std::string& value){auto wide=MetadataWide(value);if(!wide.empty())target.swap(wide);};
 replace(next.title,fields[0]);replace(next.album,fields[1]);
 const auto album_artist=MetadataWide(fields[2]),contributors=MetadataWide(fields[3]);
 if(!media.album_artist.empty()) {
  next.artist=media.album_artist;
  next.folder_artist=media.album_artist;
 } else if(!album_artist.empty())next.artist=album_artist;
 // Expanded credits are only an exact legacy-folder lookup alias. They never
 // become the album artist merely because that list has more names.
 next.legacy_artist.clear();
 if(!album_artist.empty() && album_artist!=next.artist)next.legacy_artist=album_artist;
 else if(!contributors.empty() && contributors!=next.artist)next.legacy_artist=contributors;
 const auto track=MetadataNumber(fields[4]),disc=MetadataNumber(fields[5]);
 if(track)next.track=track;
 if(disc)next.disc=disc;
 if(next.kind==MediaKind::Music||next.kind==MediaKind::VariousArtists) {
  const auto& folder_artist=next.folder_artist.empty()?next.artist:next.folder_artist;
  next.kind=_wcsicmp(folder_artist.c_str(),L"Various Artists")==0 ? MediaKind::VariousArtists : MediaKind::Music;
 }
 catalog=std::move(next);
}
void EnrichTags(const Media& media,Tags& tags) {
 const auto preferences=GetSettings();if(!preferences.metadata)return;
 RichMetadata metadata;if(!ReadCachedMetadata(media,metadata))return;
 for(const auto& field:metadata.fields) {
  if(field.first=="FLOGGFY_ARTWORK_SOURCE")continue;
  if(field.first=="LYRICS"&&!preferences.lyrics)continue;
  tags.fields.erase(std::remove_if(tags.fields.begin(),tags.fields.end(),[&](const auto& entry){return entry.first==field.first;}),tags.fields.end());
  tags.fields.push_back(field);
 }
 if(!media.album_artist.empty()) {
  tags.fields.erase(std::remove_if(tags.fields.begin(),tags.fields.end(),[](const auto& field){return field.first=="ALBUMARTIST";}),tags.fields.end());
  tags.fields.emplace_back("ALBUMARTIST",Utf8(media.album_artist));
 }
 if(!metadata.uri.empty()) {
  std::vector<uint8_t> cover;std::string mime;ImageInfo original;
  if(metadata.artwork){cover=metadata.artwork->bytes;mime=metadata.artwork->mime;}
  else if(!media.metadata)GetAlbumArtwork(metadata.uri,cover,mime); // Legacy callers without an owned record.
  if(ReadImageInfo(cover,original)) {
   tags.cover=std::move(cover);tags.mime=original.mime;
   // Provenance belongs to the validated client image actually selected above,
   // never to a cached text record or an unchanged embedded cover.
   tags.fields.erase(std::remove_if(tags.fields.begin(),tags.fields.end(),[](const auto& field){return field.first=="FLOGGFY_ARTWORK_SOURCE";}),tags.fields.end());
   tags.fields.push_back({"FLOGGFY_ARTWORK_SOURCE","spotify-client"});
  }
 }
}
static void FinishMetadataStartup(bool applied,bool activation_attempted) {
 if(!metadata_prepared)return;
 metadata_prepared=false;
 const auto now=static_cast<std::uint64_t>(GetTickCount64());
 if(applied){
  metadata_polling.store(true,std::memory_order_release);SetEvent(message_event);
  CloseHandle(metadata_preparation_thread);metadata_preparation_thread=nullptr;metadata_init.Activate();
  QueueDiagnostic("Spotify app observer ready");
  return;
 }
 // ApplyQueued can return after enabling only a prefix and resuming threads.
 // A thread may be between detour entry and its callback counter. After an
 // activation attempt, clear queued intent and disable roots but retain every
 // trampoline and callback resource for the process lifetime.
 const auto rollback=hooks::RollbackStartupHooks(metadata_startup_hooks,[]{
  for(unsigned waited=0;metadata_callbacks.Active()&&waited<5000;++waited)Sleep(1);
  return metadata_callbacks.Active()==0;
 },!activation_attempted);
 // A browser created during a partial apply can install persistent secondary
 // hooks. Keep the worker/event and callback state alive for those hooks.
 const bool dependents=display_getter.target.load()||load_getter.target.load()||console_callback.target.load()||loading_callback.target.load()||load_end_callback.target.load();
 if(!activation_attempted&&rollback.CanRelease()&&!dependents){
  metadata_polling.store(false,std::memory_order_release);metadata_running.store(false,std::memory_order_release);SetEvent(message_event);
  const bool stopped=WaitForSingleObject(metadata_preparation_thread,5000)==WAIT_OBJECT_0;
  CloseHandle(metadata_preparation_thread);metadata_preparation_thread=nullptr;
  if(stopped){CloseHandle(message_event);message_event=nullptr;LogEvent(LogKind::Warning,"warning","metadata startup activation failed; rollback complete; retry scheduled");metadata_init.Retry(now);return;}
 }
 if(metadata_preparation_thread){CloseHandle(metadata_preparation_thread);metadata_preparation_thread=nullptr;}
 LogEvent(LogKind::Warning,"warning","metadata startup activation failed or rollback unsafe; trampoline and worker state preserved and retries disabled");metadata_init.MarkUnsupported();
}
void FinalizeMetadataCollector(bool applied) {FinishMetadataStartup(applied,true);}
bool PrepareMetadataCollector(HMODULE cef) {
 const auto now=static_cast<std::uint64_t>(GetTickCount64());
 if(!metadata_init.TryBegin(now))return false;
 // Basic app playback observation stays active when optional enrichment is disabled.
 // Attempt attachment on every CEF revision. Required exports and callback
 // structure checks below determine whether the observer can attach.
 auto post_address=GetProcAddress(cef,"cef_post_task");memcpy(&post_task,&post_address,sizeof(post_task));
 if(!post_task){LogEvent(LogKind::Warning,"warning","metadata collector unsupported: cef_post_task missing");metadata_init.MarkUnsupported();return false;}
 message_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 if(!message_event){LogEvent(LogKind::Warning,"warning","metadata event allocation failed; retry scheduled");metadata_init.Retry(now);return false;}
 head=tail=count=0;
 metadata_polling.store(false,std::memory_order_release);metadata_running.store(true,std::memory_order_release);
 metadata_preparation_thread=CreateThread(nullptr,0,MetadataWorker,nullptr,0,nullptr);
 if(!metadata_preparation_thread){metadata_running.store(false,std::memory_order_release);CloseHandle(message_event);message_event=nullptr;LogEvent(LogKind::Warning,"warning","metadata worker startup failed; retry scheduled");metadata_init.Retry(now);return false;}
 metadata_prepared=true;
 auto status=MH_Initialize();if(status==MH_ERROR_ALREADY_INITIALIZED)status=MH_OK;
 if(status!=MH_OK){QueueDiagnostic("metadata MinHook initialization failed");FinishMetadataStartup(false,false);return false;}
 const char* names[]={"cef_browser_host_create_browser","cef_browser_host_create_browser_sync","cef_browser_view_create"};
 void* callbacks[]={reinterpret_cast<void*>(CreateHook),reinterpret_cast<void*>(SyncHook),reinterpret_cast<void*>(ViewHook)};
 void** originals[]={reinterpret_cast<void**>(&original_create),reinterpret_cast<void**>(&original_sync),reinterpret_cast<void**>(&original_view)};
 bool complete=true;
 for(unsigned i=0;i<3;i++){
  auto target=reinterpret_cast<void*>(GetProcAddress(cef,names[i]));if(!target)continue;
  status=MH_CreateHook(target,callbacks[i],originals[i]);
  if(status!=MH_OK){char line[200];snprintf(line,sizeof(line),"metadata hook %s create failed: %s",names[i],MH_StatusToString(status));QueueDiagnostic(line);complete=false;continue;}
  metadata_startup_hooks.Add(target);
 }
 if(!complete||!metadata_startup_hooks.Size()||!hooks::QueueStartupHooks(metadata_startup_hooks)){
  QueueDiagnostic("metadata startup hook preparation unavailable");FinishMetadataStartup(false,false);return false;
 }
 return true;
}
}
