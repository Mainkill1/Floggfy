#define WIN32_LEAN_AND_MEAN
#include "audio_history.h"
#include "ogg_history_core.h"
#include "ogg_tags.h"
#include "flac_history_core.h"
#include "history_settings.h"
#include "metadata_bridge.h"
#include "async_log.h"
#include "bounded_queue.h"
#include <atomic>
#include "library_layout.h"
#include "existing_quality.h"
#include "file_publication.h"
#include "media_session.h"
#include "vendor/minhook/include/MinHook.h"
#include <bcrypt.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

namespace {
using namespace history;
struct OggPage { unsigned char* header; int32_t header_length; unsigned char* body; int32_t body_length; };
static_assert(sizeof(OggPage)==32 && offsetof(OggPage,body)==16,"Windows x64 libogg ABI");
using PageSeek = int32_t(*)(void*,OggPage*);
PageSeek original;
void* target;
using Sniff = void*(*)(void*,const void*);
Sniff original_sniff;
void* sniff_target;
using FlacInit=void*(*)(void*,void*);
using FlacRead=int(*)(void*,unsigned char*,size_t*);
using FlacFrame=int(*)(void*,const void*,const void*);
using FlacError=void(*)(void*,unsigned);
FlacInit original_flac_init;
FlacRead original_flac_read;
FlacFrame original_flac_frame;
FlacError original_flac_error;
void* flac_targets[4]={};
struct Slot { uintptr_t context; unsigned length; double time; unsigned kind,epoch; uint64_t input_length;
    uint32_t format; bool matched; unsigned char bytes[65307]; };
constexpr unsigned capacity=128;
Slot* queue;
unsigned head=0,tail=0,count=0;
SRWLOCK queue_lock=SRWLOCK_INIT;
HANDLE event;
volatile LONG dropped=0,calls=0,pages=0;
std::wstring output;
unsigned stop_after=0;
size_t memory_limit=64*1024*1024;
static double Now() { return double(GetTickCount64())/1000.0; }
static void Log(const char* message) {
    HistoryLog(message);
}
static bool Directories(const std::wstring& path) {
    return EnsureDirectory(path);
}
static bool Supported(HMODULE module) {
    // Never scan for a loose pattern or reuse this RVA on an unknown build.
    static const unsigned char entry[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,
      0x48,0x89,0x74,0x24,0x20,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x40,0x48,0x63,0x79,0x10};
    auto base=reinterpret_cast<unsigned char*>(module);
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 || dos->e_lfanew>4096) return false;
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
       nt->OptionalHeader.SizeOfImage<=0xebf0cc || std::memcmp(base+0xebef18,entry,sizeof(entry))) return false;
    wchar_t path[2048]; if(!GetModuleFileNameW(module,path,2048)) return false;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok) ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    unsigned char buffer[65536],digest[32]; DWORD read=0;
    while(ok) {
        if(!ReadFile(file,buffer,sizeof(buffer),&read,nullptr)) { ok=false; break; }
        if(!read) break;
        if(BCryptHashData(hash,buffer,read,0)<0) ok=false;
    }
    if(ok) ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash) BCryptDestroyHash(hash);
    if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    CloseHandle(file);
    if(!ok) return false;
    const char* expected="0731eca3ec438395815907c04653c63a917b55bf0ebdd83c96f041424a92b54b";
    char hex[65]; for(unsigned i=0;i<32;i++) snprintf(hex+2*i,3,"%02x",digest[i]);
    if(std::strcmp(hex,expected)) return false;
    target=base+0xebef18; return true;
}
static int32_t Hook(void* sync,OggPage* page) {
    int32_t result=original(sync,page);
    InterlockedIncrement(&calls);
    if(!OggEnabled()) return result;
    if(result<=0 || !page || page->header_length<27 || page->header_length>282 ||
       page->body_length<0 || page->body_length>65025 ||
       page->header_length+page->body_length!=result || result>65307) return result;
    InterlockedIncrement(&pages);
    AcquireSRWLockExclusive(&queue_lock);
    if(count==capacity) InterlockedIncrement(&dropped);
    else {
        Slot& s=queue[tail]; s.context=reinterpret_cast<uintptr_t>(sync);
        s.length=unsigned(result); s.time=Now(); s.kind=0; s.epoch=CaptureEpoch();
        std::memcpy(s.bytes,page->header,page->header_length);
        if(page->body_length) std::memcpy(s.bytes+page->header_length,page->body,page->body_length);
        tail=(tail+1)%capacity; ++count;
    }
    ReleaseSRWLockExclusive(&queue_lock); SetEvent(event); return result;
}
static void* FormatHook(void* out,const void* input) {
    // Verified ABI: input is {const byte*, size_t}; result is {uint32_t kind, bool valid}.
    // Read only a short diagnostic prefix. The original detector chooses the format.
    void* result=original_sniff(out,input);
    const auto* view=static_cast<const uintptr_t*>(input);
    unsigned n=unsigned(std::min<uintptr_t>(view[1],16));
    AcquireSRWLockExclusive(&queue_lock);
    if(count==capacity) InterlockedIncrement(&dropped);
    else {
        Slot& s=queue[tail]; s.kind=1; s.length=n; s.input_length=view[1]; s.time=Now();
        std::memcpy(&s.format,out,4); s.matched=static_cast<unsigned char*>(out)[4]!=0;
        if(n) std::memcpy(s.bytes,reinterpret_cast<const void*>(view[0]),n);
        tail=(tail+1)%capacity; ++count;
    }
    ReleaseSRWLockExclusive(&queue_lock); SetEvent(event); return result;
}
static bool Pop(Slot& s) {
    AcquireSRWLockExclusive(&queue_lock);
    bool ready=count!=0;
    if(ready) { s=queue[head]; head=(head+1)%capacity; --count; }
    ReleaseSRWLockExclusive(&queue_lock); return ready;
}
static void FlacEvent(void* context,unsigned kind,const void* bytes=nullptr,size_t n=0,unsigned status=0) {
    if(!FlacEnabled()) return;
    AcquireSRWLockExclusive(&queue_lock);
    do {
        unsigned amount=unsigned(std::min(n,size_t(65307)));
        if(count==capacity) { InterlockedIncrement(&dropped); break; }
        Slot& s=queue[tail]; s.context=reinterpret_cast<uintptr_t>(context); s.kind=kind;
        s.length=amount; s.time=Now(); s.format=status; s.epoch=CaptureEpoch();
        if(amount) memcpy(s.bytes,bytes,amount);
        tail=(tail+1)%capacity; ++count;
        n-=amount;
        if(n) bytes=static_cast<const unsigned char*>(bytes)+amount;
    } while(n);
    ReleaseSRWLockExclusive(&queue_lock); SetEvent(event);
}
static void* FlacInitHook(void* context,void* result) {
    FlacEvent(context,2);
    return original_flac_init(context,result);
}
static int FlacReadHook(void* context,unsigned char* buffer,size_t* length) {
    size_t requested=*length;
    int status=original_flac_read(context,buffer,length);
    if(status==0 && *length && *length<=requested) FlacEvent(context,3,buffer,*length);
    else if(status==1 && !*length) FlacEvent(context,6);
    else if(status!=0 || *length>requested) FlacEvent(context,5,nullptr,0,unsigned(status));
    return status;
}
static int FlacFrameHook(void* context,const void* frame,const void* pcm) {
    int status=original_flac_frame(context,frame,pcm);
    if(status==0) FlacEvent(context,4,frame,32);
    else FlacEvent(context,5,nullptr,0,unsigned(status));
    return status;
}
static void FlacErrorHook(void* context,unsigned error) {
    original_flac_error(context,error);
    FlacEvent(context,5,nullptr,0,error);
}
struct Capture {
    Stream stream;
    bool lossless=false,metadata=false;
    FlacInfo flac;
    FlacCoverage coverage;
    double born=0,finished=0;
    uint64_t bytes=0;
    CompressedBuffer data;
    double Duration() const { return lossless && flac.rate ? double(coverage.samples)/flac.rate : stream.Duration(); }
    Quality Encoding() const { return lossless ? Quality{Codec::Flac,flac.rate,flac.channels,flac.bits,0} : Quality{Codec::Vorbis,stream.rate,stream.channels,0,stream.bitrate}; }
};
struct Heard { Media media; double start=0,finish=0; };
static Catalog CatalogFor(const Media& media) {
    Catalog c; c.title=media.title; c.album=media.album; c.track=media.track;
    c.artist=media.album_artist.empty() ? media.artist : media.album_artist;
    if(_wcsicmp(c.artist.c_str(),L"Various Artists")==0) c.kind=MediaKind::VariousArtists;
    return c;
}
enum class Publication { Saved,Skipped,Failed };
static Publication Publish(Capture& c,const Heard& heard,unsigned expected_epoch) {
    SYSTEMTIME t; GetSystemTime(&t); wchar_t stamp[64];
    swprintf(stamp,64,L"%04u-%02u-%02uT%02u:%02u:%02uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
    auto catalog=CatalogFor(heard.media);
    auto settings=GetSettings();
    if(settings.capture_epoch!=expected_epoch || !settings.downloads || !(c.lossless ? settings.flac : settings.ogg)) return Publication::Skipped;
    std::wstring destination=OutputPath(settings.root,catalog,c.lossless ? L".flac" : L".ogg",settings.music_folder);
    std::wstring flac=OutputPath(settings.root,catalog,L".flac",settings.music_folder);
    Quality quality=c.Encoding(),lossless;
    if(!c.lossless && ReadQuality(flac,lossless) && lossless.codec==Codec::Flac) {
        Log(("SKIP existing lossless file "+Utf8(flac)).c_str()); return Publication::Skipped;
    }
    SaveDecision decision=DecideSave(destination,quality);
    if(decision==SaveDecision::Skip) {
        Log(("SKIP existing equal, higher or unverified quality "+Utf8(destination)).c_str()); return Publication::Skipped;
    }
    Tags tags;
    tags.fields={{"TITLE",Utf8(heard.media.title)},{"ARTIST",Utf8(heard.media.artist)},
      {"ALBUM",Utf8(heard.media.album)},{"ALBUMARTIST",Utf8(heard.media.album_artist)},
      {"TRACKNUMBER",std::to_string(heard.media.track)},
      {"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_TRANSCODED","0"},
      {"HISTORY_SAMPLES",std::to_string(c.lossless ? c.coverage.samples : c.stream.samples)},
      {"HISTORY_SAMPLE_RATE",std::to_string(quality.rate)},
      {"HISTORY_BITS_PER_SAMPLE",std::to_string(quality.bits)},
      {"HISTORY_NOMINAL_BITRATE",std::to_string(quality.bitrate)},
      {"HISTORY_MEDIA_DURATION",std::to_string(heard.media.duration)},
      {"HISTORY_SOURCE_PAGES",std::to_string(c.lossless ? c.coverage.frames : c.stream.pages)},
      {"HISTORY_SOURCE_BYTES",std::to_string(c.bytes)},
      {"HISTORY_ELAPSED_SECONDS",std::to_string(heard.finish-heard.start)},
      {"HISTORY_SAVED_UTC",Utf8(stamp)},{"HISTORY_CAPTURE",c.lossless ? "native compressed FLAC input" : "native compressed Ogg pages"}};
    tags.cover=heard.media.cover; tags.mime=heard.media.cover_extension==L".png" ? "image/png" : "image/jpeg";
    if(!heard.media.genre.empty())tags.fields.push_back({"GENRE",Utf8(heard.media.genre)});
    EnrichTags(heard.media,tags);
    std::vector<uint8_t> tagged; std::string error;
    auto source=c.data.Flatten();
    bool tagged_ok=c.lossless ? TagFlac(source,tags,tagged,error) : TagOgg(source,tags,tagged,error);
    if(!tagged_ok) { Log(("native tagging failed: "+error).c_str()); return Publication::Failed; }
    auto latest=GetSettings();
    if(latest.generation!=settings.generation || !latest.downloads || !(c.lossless ? latest.flac : latest.ogg)) return Publication::Skipped;
    if(!Directories(destination.substr(0,destination.find_last_of(L'\\')))) return Publication::Failed;
    // First and only audio disk write: the complete, tagged file. No scratch
    // paths, sidecars, per-song directories, decoder or encoder are involved.
    auto publication=PublishBytes(destination,quality,tagged.data(),tagged.size());
    if(publication==FilePublication::Skipped) return Publication::Skipped;
    if(publication==FilePublication::Failed) { Log("final publication failed; existing file retained"); return Publication::Failed; }
    std::string line=(publication==FilePublication::Upgraded ? "UPGRADED " : "SAVED ")+Utf8(destination); Log(line.c_str()); return Publication::Saved;
}
struct SaveJob {std::unique_ptr<Capture> capture;Heard heard;unsigned epoch=0;size_t reserved=0;};
static BoundedQueue<SaveJob,2> saves;
static HANDLE save_event=nullptr;
static std::atomic<size_t> publishing_reserved{0};
static std::atomic<unsigned> saved_count{0};
static DWORD WINAPI SaveWorker(LPVOID) {
    for(;;) {
        SaveJob job;
        if(!saves.Pop(job)){WaitForSingleObject(save_event,200);continue;}
        std::string identity=Utf8(job.heard.media.artist)+" - "+Utf8(job.heard.media.title);
        try {
            auto result=Publish(*job.capture,job.heard,job.epoch);
            if(result==Publication::Saved) {
                auto settings=GetSettings();
                if(settings.capture_epoch==job.epoch) {unsigned count=++saved_count;if(settings.stop_after && count>=settings.stop_after)SetDownloads(false);}
                LogActivity("finished",identity);
            } else LogActivity(result==Publication::Failed?"failed":"finished",identity+(result==Publication::Failed?" (publication failed)":" (skipped existing file or changed settings)"));
        } catch(...) {LogActivity("failed",identity+" (publication exception)");}
        job.capture.reset();publishing_reserved.fetch_sub(job.reserved);
    }
}
static DWORD WINAPI Worker(LPVOID) {
    LONG overflow=0;
    std::map<uintptr_t,std::unique_ptr<Capture>> active;
    std::vector<std::unique_ptr<Capture>> ready;
    std::vector<Heard> heard;
    Listen listen; MediaReader reader; Media current;
    double next_media=0,next_log=0;
    unsigned generation=~0u,epoch=~0u; bool enabled=false;
    try {
        for(;;) {
            double now=Now();
            auto preferences=GetSettings();
            if(preferences.generation!=generation) {
                bool initial=generation==~0u;
                generation=preferences.generation;
                output=preferences.root; memory_limit=size_t(preferences.max_buffered_mib)*1024*1024; stop_after=preferences.stop_after;
                bool capture=preferences.downloads && (preferences.ogg || preferences.flac);
                if(capture!=enabled || initial || preferences.capture_epoch!=epoch) {
                    active.clear(); ready.clear(); heard.clear(); listen={}; current={}; next_media=0;
                    if(!enabled && capture) saved_count=0;
                    enabled=capture;
                    Log(enabled ? "To Disk capture enabled" : "To Disk capture disabled");
                }
                epoch=preferences.capture_epoch;
            }
            if(!enabled) {
                Slot discard; while(Pop(discard)) {}
                WaitForSingleObject(event,200); continue;
            }
            if(dropped!=overflow) {
                overflow=dropped; active.clear(); ready.clear(); heard.clear(); listen.eligible=false;
                Log("capture queue overflow; streams and listening coverage invalidated");
            }
            Slot s;
            while(Pop(s)) {
                if(s.kind==1) {
                    char prefix[33]={}; for(unsigned i=0;i<s.length;i++) snprintf(prefix+2*i,3,"%02x",s.bytes[i]);
                    char line[180]; snprintf(line,sizeof(line),"FORMAT kind=%u matched=%d input_bytes=%llu prefix=%s",
                      s.format,s.matched,static_cast<unsigned long long>(s.input_length),prefix); Log(line); continue;
                }
                if(s.epoch!=epoch) continue;
                if(s.kind>=2) {
                    auto found=active.find(s.context);
                    if(s.kind==2) {
                        auto c=std::make_unique<Capture>(); c->lossless=true; c->born=s.time;
                        active[s.context]=std::move(c); Log("FLAC decoder initialized; capture begins at compressed offset zero"); continue;
                    }
                    if(found==active.end() || !found->second->lossless) continue;
                    Capture& c=*found->second;
                    if(s.kind==5) { active.erase(found); Log("FLAC decoder error; stream discarded"); continue; }
                    if(s.kind==3) {
                        size_t reserved=publishing_reserved.load(); for(const auto& item:active) reserved+=item.second->data.capacity();
                        for(const auto& item:ready) reserved+=item->data.capacity();
                        size_t available=reserved<memory_limit ? memory_limit-reserved : 0;
                        if(!c.data.Append(s.bytes,s.length,available)) { active.erase(found); Log("FLAC capture memory limit; stream discarded"); continue; }
                        c.bytes+=s.length;
                        if(!c.metadata) {
                            size_t audio=0; auto parsed=ParseFlacMetadata(c.data,c.flac,audio);
                            if(parsed==FlacParse::Invalid) { active.erase(found); Log("FLAC metadata invalid; stream discarded"); continue; }
                            if(parsed==FlacParse::Valid) {
                                c.metadata=true; char line[180]; snprintf(line,sizeof(line),"FLAC STREAMINFO rate=%u channels=%u bits=%u total=%llu metadata_bytes=%zu",c.flac.rate,c.flac.channels,c.flac.bits,static_cast<unsigned long long>(c.flac.total_samples),audio); Log(line);
                            }
                        }
                        continue;
                    }
                    if(s.kind==4) {
                        uint32_t frame[6]; uint64_t number; memcpy(frame,s.bytes,24); memcpy(&number,s.bytes+24,8);
                        if(frame[5]==0) number=uint32_t(number);
                        if(!c.metadata || !c.coverage.Frame(c.flac,frame[0],frame[1],frame[2],frame[4],frame[5],number)) {
                            active.erase(found); Log("FLAC decoded-frame gap or format mismatch; stream discarded"); continue;
                        }
                        if(!c.coverage.Complete(c.flac)) continue;
                    } else if(s.kind!=6 || !c.coverage.Complete(c.flac)) { active.erase(found); Log("FLAC truncated before total samples; discarded"); continue; }
                    c.finished=s.time;
                    char line[180]; snprintf(line,sizeof(line),"FLAC COMPLETE frames=%u samples=%llu bytes=%llu duration=%.6f",c.coverage.frames,static_cast<unsigned long long>(c.coverage.samples),static_cast<unsigned long long>(c.bytes),c.Duration()); Log(line);
                    ready.push_back(std::move(found->second)); active.erase(found); continue;
                }
                Page page; if(!ParsePage(s.bytes,s.length,page)) {
                    active.erase(s.context); Log("invalid Ogg page rejected"); continue;
                }
                if(page.vorbis_start) {
                    auto c=std::make_unique<Capture>(); c->born=s.time;
                    active[s.context]=std::move(c);
                    char line[160]; snprintf(line,sizeof(line),"BOS ctx=%llx seq=%u serial=%u rate=%u channels=%u",
                        static_cast<unsigned long long>(s.context),page.sequence,page.serial,page.rate,page.channels); Log(line);
                }
                auto found=active.find(s.context); if(found==active.end()) continue;
                Capture& c=*found->second; Result result=c.stream.Push(s.bytes,s.length);
                if(result==Result::Invalid || result==Result::Ignore) {
                    active.erase(found); Log("stream discarded: page gap or integrity failure"); continue;
                }
                size_t reserved=publishing_reserved.load();
                for(const auto& item:active) reserved+=item.second->data.capacity();
                for(const auto& item:ready) reserved+=item->data.capacity();
                size_t available=reserved<memory_limit ? memory_limit-reserved : 0;
                if(!c.data.Append(s.bytes,s.length,available)) {
                    active.erase(found);
                    Log("capture memory limit reached; track discarded without scratch files"); continue;
                }
                c.bytes+=s.length;
                if(result==Result::Complete) {
                    c.finished=s.time;
                    char line[200]; snprintf(line,sizeof(line),"EOS pages=%u bytes=%llu duration=%.6f capture_seconds=%.3f",
                      c.stream.pages,static_cast<unsigned long long>(c.bytes),c.stream.Duration(),c.finished-c.born); Log(line);
                    ready.push_back(std::move(found->second)); active.erase(found);
                }
            }
            if(now>=next_media) {
                Media media;
                if(reader.Read(media)) {
                    std::string previous=listen.identity; double previous_start=listen.start_time;
                    bool was_eligible=listen.eligible;
                    double prior_position=listen.last_position,prior_duration=listen.duration,prior_time=listen.last_time;
                    std::string done=listen.Observe(media.Key(),media.position,media.duration,media.playing,Now());
                    if(was_eligible && !listen.eligible && done.empty()) {
                        char line[300]; snprintf(line,sizeof(line),"listen invalidated old=%.6f/%.6f new=%.6f/%.6f delta=%.3f playing=%d",
                            prior_position,prior_duration,media.position,media.duration,Now()-prior_time,media.playing); Log(line);
                    }
                    if(!done.empty()) { heard.push_back({current,previous_start,Now()}); Log(("FULL LISTEN "+Utf8(current.title)).c_str()); }
                    if(previous!=listen.identity && listen.eligible) LogActivity("started",Utf8(media.artist)+" - "+Utf8(media.title));
                    if(was_eligible && !listen.eligible && done.empty()) LogActivity("failed",Utf8(current.artist)+" - "+Utf8(current.title)+" (incomplete listen)");
                    if(previous!=listen.identity) {
                        Log(("TRACK "+Utf8(media.artist)+" - "+Utf8(media.title)).c_str());
                        {
                            auto prior=[previous_start,start=listen.start_time](const auto& c) {
                                return c->born>=previous_start-20 && c->born<=previous_start+3 && c->born<start-3;
                            };
                            // A fast Next may create the new decoder inside the
                            // previous track's three-second start window. Keep
                            // that candidate; duration association still rejects
                            // ambiguous or skipped streams at publication.
                            if(done.empty())
                            ready.erase(std::remove_if(ready.begin(),ready.end(),prior),ready.end());
                            for(auto it=active.begin();it!=active.end();) {
                                if(prior(it->second)) it=active.erase(it); else ++it;
                            }
                        }
                    }
                    if(listen.transient) Log("timeline reset at natural end; waiting for matching title");
                    else current=std::move(media);
                } else { listen.eligible=false; Log("Spotify media snapshot unavailable; listen invalidated"); }
                next_media=Now()+0.5;
            }
            // A media-session read may block while the menu changes settings.
            // Apply that generation before considering any publication.
            if(GetSettings().generation!=generation) continue;
            for(auto h=heard.begin();h!=heard.end();) {
                size_t match=ready.size(),matches=0;
                for(size_t i=0;i<ready.size();i++) {
                    Capture& c=*ready[i];
                    if(std::fabs(c.Duration()-h->media.duration)<=1.0 &&
                       c.born>=h->start-20 && c.born<=h->start+3) { match=i; ++matches; }
                }
                if(matches==1) {
                    SaveJob job;job.reserved=ready[match]->data.capacity();job.epoch=epoch;
                    job.capture=std::move(ready[match]);job.heard=*h;
                    publishing_reserved.fetch_add(job.reserved);
                    if(saves.Push(std::move(job)))SetEvent(save_event);
                    else {size_t reserved=job.reserved;job.capture.reset();publishing_reserved.fetch_sub(reserved);LogActivity("failed","publication queue full; completed candidate discarded");}
                    ready.erase(ready.begin()+match); h=heard.erase(h);
                } else if(matches>1 || now-h->finish>10) {
                    Log("completed listen discarded: missing or ambiguous Ogg association"); h=heard.erase(h);
                } else ++h;
            }
            ready.erase(std::remove_if(ready.begin(),ready.end(),[now](const auto& c){return now-c->finished>600;}),ready.end());
            for(auto it=active.begin();it!=active.end();) {
                if(now-it->second->born>21600) it=active.erase(it); else ++it;
            }
            if(now>=next_log) {
                size_t buffered=0;
                for(const auto& item:active) buffered+=item.second->data.capacity();
                for(const auto& item:ready) buffered+=item->data.capacity();
                char line[350]; snprintf(line,sizeof(line),"status calls=%ld pages=%ld dropped=%ld active=%zu ready=%zu saved=%u pos=%.3f/%.3f eligible=%d buffered=%zu",
                  calls,pages,dropped,active.size(),ready.size(),saved_count.load(),current.position,current.duration,listen.eligible,buffered); Log(line); next_log=now+10;
            }
            if(stop_after && saved_count.load()>=stop_after) { Log("showcase limit reached; Downloads disabled"); SetDownloads(false); }
            WaitForSingleObject(event,50);
        }
    } catch(...) { Log("history worker stopped after an exception; playback remains with original parser"); }
    MH_DisableHook(target);
    MH_DisableHook(sniff_target);
    for(auto address:flac_targets) if(address) MH_DisableHook(address);
    return 0;
}
}

void StartAudioHistory(HMODULE spotify,HMODULE proxy) {
    static bool attempted=false; if(attempted) return; attempted=true;
    (void)proxy;
    auto preferences=GetSettings(); output=preferences.root;
    if(output.empty()) { Log("save location unavailable; history disabled"); return; }
    Log("native history enabled; memory capture; embedded metadata/art; no scratch files; no FFmpeg; complete listens only");
    if(!Supported(spotify)) { Log("unsupported Spotify.dll hash or parser prologue; capture disabled"); return; }
    save_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!save_event){Log("publication worker event failed");return;}
    auto publisher=CreateThread(nullptr,0,SaveWorker,nullptr,0,nullptr);
    if(!publisher){Log("publication worker startup failed");return;}SetThreadPriority(publisher,THREAD_PRIORITY_BELOW_NORMAL);CloseHandle(publisher);
    queue=static_cast<Slot*>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Slot)*capacity));
    event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!queue || !event) { Log("queue allocation failed; capture disabled"); return; }
    MH_STATUS status=MH_Initialize(); if(status==MH_ERROR_ALREADY_INITIALIZED) status=MH_OK;
    if(status==MH_OK) status=MH_CreateHook(target,reinterpret_cast<void*>(Hook),reinterpret_cast<void**>(&original));
    sniff_target=reinterpret_cast<unsigned char*>(spotify)+0xe9c550;
    if(status==MH_OK) status=MH_CreateHook(sniff_target,reinterpret_cast<void*>(FormatHook),reinterpret_cast<void**>(&original_sniff));
    const uintptr_t flac_rvas[]={0xe95f2c,0xe9663c,0xe969f4,0xe959d0};
    void* flac_callbacks[]={reinterpret_cast<void*>(FlacInitHook),reinterpret_cast<void*>(FlacReadHook),reinterpret_cast<void*>(FlacFrameHook),reinterpret_cast<void*>(FlacErrorHook)};
    void** flac_originals[]={reinterpret_cast<void**>(&original_flac_init),reinterpret_cast<void**>(&original_flac_read),reinterpret_cast<void**>(&original_flac_frame),reinterpret_cast<void**>(&original_flac_error)};
    for(unsigned i=0;status==MH_OK && i<4;i++) {
        flac_targets[i]=reinterpret_cast<unsigned char*>(spotify)+flac_rvas[i];
        status=MH_CreateHook(flac_targets[i],flac_callbacks[i],flac_originals[i]);
        if(status==MH_OK) status=MH_QueueEnableHook(flac_targets[i]);
    }
    if(status==MH_OK) status=MH_QueueEnableHook(target);
    if(status==MH_OK) status=MH_QueueEnableHook(sniff_target);
    if(status==MH_OK) status=MH_ApplyQueued();
    char message[180]; snprintf(message,sizeof(message),"parser hook RVA=0xEBEF18 status=%s",MH_StatusToString(status)); Log(message);
    if(status!=MH_OK) { MH_DisableHook(target); MH_DisableHook(sniff_target); for(auto address:flac_targets) if(address) MH_DisableHook(address); return; }
    HANDLE worker=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);
    if(worker) CloseHandle(worker);
    else { MH_DisableHook(target); MH_DisableHook(sniff_target); for(auto address:flac_targets) if(address) MH_DisableHook(address); Log("worker thread creation failed; capture disabled"); }
}
