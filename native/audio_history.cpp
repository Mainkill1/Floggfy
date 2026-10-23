#define WIN32_LEAN_AND_MEAN
#include "audio_history.h"
#include "ogg_history_core.h"
#include "ogg_tags.h"
#include "flac_history_core.h"
#include "history_settings.h"
#include "hook_init_state.h"
#include "hook_rollback.h"
#include "metadata_bridge.h"
#include "playback_quality.h"
#include "async_log.h"
#include "bounded_queue.h"
#include <atomic>
#include "library_layout.h"
#include "existing_quality.h"
#include "file_publication.h"
#include "recent_downloads.h"
#include "song_destination.h"
#include "lyrics_update.h"
#include "media_session.h"
#include "spotify_hook_discovery.h"
#include "vendor/minhook/include/MinHook.h"
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
hooks::CallbackCounter audio_callbacks;
enum class AudioEventKind : unsigned { OggPage,FormatDetected,FlacBegin,FlacBytes,FlacFrame,FlacError,FlacEnd };
struct Slot { uintptr_t context; unsigned length; double time; AudioEventKind kind;unsigned epoch; uint64_t input_length;
    uint32_t format; bool matched,capture; unsigned char bytes[65307]; };
constexpr unsigned capacity=128;
Slot* queue;
unsigned head=0,tail=0,count=0;
SRWLOCK queue_lock=SRWLOCK_INIT;
HANDLE event;
volatile LONG dropped=0,calls=0,pages=0;
std::atomic<bool> quality_enabled{false};
std::wstring output;
size_t memory_limit=500*1024*1024;
static double Now() { return double(GetTickCount64())/1000.0; }
static void Log(const char* message) {
    HistoryLog(message);
}
static bool Directories(const std::wstring& path) {
    return EnsureDirectory(path);
}
static spotify::DiscoveryResult DiscoverTargets(HMODULE module,spotify::HookTargets& targets) {
    auto* base=reinterpret_cast<unsigned char*>(module);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 || dos->e_lfanew>4096)
        return spotify::DiscoveryResult::InvalidImage;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64)
        return spotify::DiscoveryResult::InvalidImage;
    return spotify::DiscoverHookTargets(base,nt->OptionalHeader.SizeOfImage,targets);
}

static const char* DiscoveryFailure(spotify::DiscoveryResult result) {
    switch(result) {
        case spotify::DiscoveryResult::InvalidImage:return "invalid PE image";
        case spotify::DiscoveryResult::MissingTarget:return "required instruction anchor missing";
        case spotify::DiscoveryResult::AmbiguousTarget:return "instruction anchor is ambiguous";
        case spotify::DiscoveryResult::InconsistentLayout:return "target layout failed structural checks";
        case spotify::DiscoveryResult::Found:return "none";
    }
    return "unknown discovery failure";
}

static void* DiscoveredTarget(HMODULE module,const spotify::HookTargets& targets,
                              spotify::TargetKind kind) {
    const auto rva=spotify::TargetRva(targets,kind);
    return rva?reinterpret_cast<unsigned char*>(module)+rva:nullptr;
}
static int32_t Hook(void* sync,OggPage* page) {
    auto callback=audio_callbacks.Enter();
    int32_t result=original(sync,page);
    InterlockedIncrement(&calls);
    const bool capture=OggEnabled();
    if(!capture && !quality_enabled.load(std::memory_order_relaxed)) return result;
    if(result<=0 || !page || page->header_length<27 || page->header_length>282 ||
       page->body_length<0 || page->body_length>65025 ||
       page->header_length+page->body_length!=result || result>65307) return result;
    InterlockedIncrement(&pages);
    AcquireSRWLockExclusive(&queue_lock);
    if(count==capacity) InterlockedIncrement(&dropped);
    else {
        Slot& s=queue[tail]; s.context=reinterpret_cast<uintptr_t>(sync);
        s.length=unsigned(result); s.time=Now(); s.kind=AudioEventKind::OggPage; s.epoch=CaptureEpoch(); s.capture=capture;
        std::memcpy(s.bytes,page->header,page->header_length);
        if(page->body_length) std::memcpy(s.bytes+page->header_length,page->body,page->body_length);
        tail=(tail+1)%capacity; ++count;
    }
    ReleaseSRWLockExclusive(&queue_lock); SetEvent(event); return result;
}
static void* FormatHook(void* out,const void* input) {
    auto callback=audio_callbacks.Enter();
    // Verified ABI: input is {const byte*, size_t}; result is {uint32_t kind, bool valid}.
    // Read only a short diagnostic prefix. The original detector chooses the format.
    void* result=original_sniff(out,input);
    const auto* view=static_cast<const uintptr_t*>(input);
    unsigned n=unsigned(std::min<uintptr_t>(view[1],16));
    AcquireSRWLockExclusive(&queue_lock);
    if(count==capacity) InterlockedIncrement(&dropped);
    else {
        Slot& s=queue[tail]; s.kind=AudioEventKind::FormatDetected; s.length=n; s.input_length=view[1]; s.time=Now();
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
static void FlacEvent(void* context,AudioEventKind kind,const void* bytes=nullptr,size_t n=0,unsigned status=0) {
    const bool capture=FlacEnabled();
    if(!capture && !quality_enabled.load(std::memory_order_relaxed)) return;
    AcquireSRWLockExclusive(&queue_lock);
    do {
        unsigned amount=unsigned(std::min(n,size_t(65307)));
        if(count==capacity) { InterlockedIncrement(&dropped); break; }
        Slot& s=queue[tail]; s.context=reinterpret_cast<uintptr_t>(context); s.kind=kind;
        s.capture=capture; s.input_length=amount;
        s.length=(!capture && kind==AudioEventKind::FlacBytes) ? std::min(amount,42u) : amount;
        s.time=Now(); s.format=status; s.epoch=CaptureEpoch();
        if(s.length) memcpy(s.bytes,bytes,s.length);
        tail=(tail+1)%capacity; ++count;
        n-=amount;
        if(n) bytes=static_cast<const unsigned char*>(bytes)+amount;
    } while(n);
    ReleaseSRWLockExclusive(&queue_lock); SetEvent(event);
}
static void* FlacInitHook(void* context,void* result) {
    auto callback=audio_callbacks.Enter();
    FlacEvent(context,AudioEventKind::FlacBegin);
    return original_flac_init(context,result);
}
static int FlacReadHook(void* context,unsigned char* buffer,size_t* length) {
    auto callback=audio_callbacks.Enter();
    size_t requested=*length;
    int status=original_flac_read(context,buffer,length);
    if(status==0 && *length && *length<=requested) FlacEvent(context,AudioEventKind::FlacBytes,buffer,*length);
    else if(status==1 && !*length) FlacEvent(context,AudioEventKind::FlacEnd);
    else if(status!=0 || *length>requested) FlacEvent(context,AudioEventKind::FlacError,nullptr,0,unsigned(status));
    return status;
}
static int FlacFrameHook(void* context,const void* frame,const void* pcm) {
    auto callback=audio_callbacks.Enter();
    int status=original_flac_frame(context,frame,pcm);
    if(status==0) FlacEvent(context,AudioEventKind::FlacFrame,frame,32);
    else FlacEvent(context,AudioEventKind::FlacError,nullptr,0,unsigned(status));
    return status;
}
static void FlacErrorHook(void* context,unsigned error) {
    auto callback=audio_callbacks.Enter();
    original_flac_error(context,error);
    FlacEvent(context,AudioEventKind::FlacError,nullptr,0,error);
}
struct Capture {
    Stream stream;
    bool lossless=false,metadata=false;
    FlacInfo flac;
    FlacCoverage coverage;
    double born=0,finished=0;
    uint64_t bytes=0,listen_generation=0;
    CompressedBuffer data;
    double Duration() const { return lossless && flac.rate ? double(coverage.samples)/flac.rate : stream.Duration(); }
    Quality Encoding() const { return lossless ? Quality{Codec::Flac,flac.rate,flac.channels,flac.bits,0} : Quality{Codec::Vorbis,stream.rate,stream.channels,0,stream.bitrate}; }
};
struct Heard { Media media; double start=0,finish=0;std::uint64_t generation=0; };
static Catalog CatalogFor(const Media& media) {
    Catalog c; c.title=media.title; c.album=media.album; c.track=media.track;c.duration=media.duration;
    c.artist=media.album_artist.empty() ? media.artist : media.album_artist;
    if(_wcsicmp(c.artist.c_str(),L"Various Artists")==0) c.kind=MediaKind::VariousArtists;
    EnrichCatalog(media,c);return c;
}
static void PublishCurrentDestination(const Media& media,const Settings& settings) {
    PublishSongDestination(media.title.empty() ? 0 : PlaybackIdentity(media.Key()),
                           settings.root,CatalogFor(media),settings.music_folder,settings.generation);
}
static std::string Grouped(unsigned value) {
    auto text=std::to_string(value);
    for(int p=int(text.size())-3;p>0;p-=3)text.insert(size_t(p),",");
    return text;
}
static std::string SavedAudioDetails(const Quality& quality,size_t bytes) {
    std::string text=quality.codec==Codec::Flac ? "FLAC" : "Ogg Vorbis";
    if(quality.codec==Codec::Flac && quality.bits)text+=" "+std::to_string(quality.bits)+"-bit";
    if(quality.codec==Codec::Vorbis && quality.bitrate) {
        text+=quality.bitrate%1000 ? " "+std::to_string(quality.bitrate)+" bps nominal" : " "+std::to_string(quality.bitrate/1000)+" kbps nominal";
    }
    if(quality.rate)text+=", "+Grouped(quality.rate)+" Hz";
    char size[96];snprintf(size,sizeof(size),"%.2f MiB (%llu bytes)",double(bytes)/(1024*1024),static_cast<unsigned long long>(bytes));
    return text+" | "+size;
}
static std::string ExistingSkipReason(const std::wstring& path,const Quality& incoming) {
    Quality existing;
    if(!ReadQuality(path,existing))return "existing file quality could not be verified; retained";
    const bool same=existing.codec==incoming.codec && existing.rate && existing.rate==incoming.rate &&
        existing.channels && existing.channels==incoming.channels &&
        (existing.codec==Codec::Flac ? existing.bits && existing.bits==incoming.bits : existing.bitrate && existing.bitrate==incoming.bitrate);
    if(same)return "same quality already exists on disk";
    if(HigherQuality(existing,incoming))return "better quality already exists on disk";
    return "existing file retained; incoming audio is not a verified quality upgrade";
}
static void SetIdentityTag(Tags& tags,const char* name,const std::string& value) {
    tags.fields.erase(std::remove_if(tags.fields.begin(),tags.fields.end(),[&](const auto& field){return field.first==name;}),tags.fields.end());
    if(!value.empty()) tags.fields.emplace_back(name,value);
}
static void ApplyCatalogIdentity(Tags& tags,const Catalog& catalog) {
    SetIdentityTag(tags,"TITLE",Utf8(catalog.title));SetIdentityTag(tags,"ALBUM",Utf8(catalog.album));
    SetIdentityTag(tags,"ALBUMARTIST",Utf8(catalog.artist));
    SetIdentityTag(tags,"TRACKNUMBER",catalog.track ? std::to_string(catalog.track) : std::string{});
    SetIdentityTag(tags,"DISCNUMBER",catalog.disc ? std::to_string(catalog.disc) : std::string{});
    SetIdentityTag(tags,"SPOTIFY_URI",catalog.spotify_uri);
}
enum class Publication { Saved,Skipped,Failed };
static Publication Publish(Capture& c,const Heard& heard,unsigned expected_epoch) {
    const std::string identity=Utf8(heard.media.artist)+" - "+Utf8(heard.media.title);
    auto finish=[&](Publication status,const char* event,const std::string& detail) {
        // Put outcome facts first so a long title cannot truncate quality/size.
        LogActivity(event,detail+" | "+identity);return status;
    };
    SYSTEMTIME t; GetSystemTime(&t); wchar_t stamp[64];
    swprintf(stamp,64,L"%04u-%02u-%02uT%02u:%02u:%02uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
    auto catalog=CatalogFor(heard.media);
    auto settings=GetSettings();
    if(settings.capture_epoch!=expected_epoch || !settings.downloads || !(c.lossless ? settings.flac : settings.ogg)) return finish(Publication::Skipped,"skipped","capture settings changed or downloads/format disabled");
    Quality quality=c.Encoding(),lossless;
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
    // Use the same RAM-only canonical catalog as Song probing. Enrichment may
    // arrive while a completed listen waits for publication; make final tags
    // and path planning agree before inspecting any existing recording.
    catalog=CatalogFor(heard.media);ApplyCatalogIdentity(tags,catalog);
    const auto destination=ResolveRecordingPath(OutputPath(settings.root,catalog,c.lossless ? L".flac" : L".ogg",settings.music_folder),catalog);
    if(destination.empty())return finish(Publication::Failed,"failed","no safe recording destination; existing files retained");
    const auto flac=c.lossless ? destination : ResolveRecordingPath(OutputPath(settings.root,catalog,L".flac",settings.music_folder),catalog);
    const bool existing_lossless=!c.lossless && !flac.empty() && ExistingRecording(flac,catalog,lossless) && lossless.codec==Codec::Flac;
    const auto decision=existing_lossless ? SaveDecision::Skip : DecideSave(destination,quality,catalog);
    if(decision==SaveDecision::Skip) {
        // An audio duplicate may still be a metadata upgrade. Always patch
        // the existing FLAC, including when this listen used lossy playback.
        const auto latest=GetSettings();
        if(settings.metadata && settings.lyrics && settings.flac && (c.lossless || existing_lossless) &&
           latest.generation==settings.generation && latest.downloads && latest.lyrics && latest.metadata && latest.flac) {
            const auto& path=existing_lossless ? flac : destination;
            RecordingFile existing;
            const auto result=ReadRecording(path,existing) && existing.owned && existing.complete && MatchesRecording(existing,catalog) ?
                UpdateFlacLyrics(path,tags) : FileLyricsUpdate::Skipped;
            if(result==FileLyricsUpdate::Updated){RefreshSongDestination();Log(("LYRICS UPDATED "+Utf8(path)).c_str());return finish(Publication::Saved,"updated","lyrics updated; existing audio retained");}
            if(result==FileLyricsUpdate::Failed)return finish(Publication::Failed,"failed","lyric update failed; existing file retained");
        }
        Log(("SKIP existing equal, higher or unverified quality "+Utf8(existing_lossless ? flac : destination)).c_str());
        return finish(Publication::Skipped,"skipped",ExistingSkipReason(existing_lossless ? flac : destination,quality));
    }
    if(c.lossless && decision==SaveDecision::Upgrade && settings.metadata && settings.lyrics)
        PreserveFlacLyrics(destination,tags);
    std::vector<uint8_t> tagged; std::string error;
    auto source=c.data.Flatten();
    bool tagged_ok=c.lossless ? TagFlac(source,tags,tagged,error) : TagOgg(source,tags,tagged,error);
    if(!tagged_ok)return finish(Publication::Failed,"failed","tagging failed: "+error);
    auto latest=GetSettings();
    if(latest.generation!=settings.generation || !latest.downloads || !(c.lossless ? latest.flac : latest.ogg)) return finish(Publication::Skipped,"skipped","capture settings changed or downloads/format disabled");
    if(!Directories(destination.substr(0,destination.find_last_of(L'\\')))) return finish(Publication::Failed,"failed","could not create save directory");
    // Only complete tagged audio reaches the bounded save worker's staged,
    // flushed publication protocol. Recording identity authorizes replacement.
    auto publication=PublishBytes(destination,quality,tagged.data(),tagged.size(),catalog);
    if(publication==FilePublication::Skipped)return finish(Publication::Skipped,"skipped",ExistingSkipReason(destination,quality));
    if(publication==FilePublication::Failed)return finish(Publication::Failed,"failed","file publication failed; existing file retained");
    RefreshSongDestination();
    ImageInfo artwork;
    if(!ReadImageInfo(tags.cover,artwork))LogActivity("metadata","artwork unavailable; audio and text tags saved | "+identity);
    RecordRecentDownload(heard.media.artist+L" - "+heard.media.title,destination,tagged.size());
    std::string line=(publication==FilePublication::Upgraded ? "UPGRADED " : "SAVED ")+Utf8(destination); Log(line.c_str());
    return finish(Publication::Saved,publication==FilePublication::Upgraded ? "upgraded" : "finished",SavedAudioDetails(quality,tagged.size()));
}
struct SaveJob {std::unique_ptr<Capture> capture;Heard heard;unsigned epoch=0;size_t reserved=0;};
static BoundedQueue<SaveJob,2> saves;
static HANDLE save_event=nullptr;
static std::atomic<size_t> publishing_reserved{0};
static std::atomic<unsigned> saved_count{0};
static std::atomic<bool> workers_running{false};
static hooks::InitController audio_init;
static DWORD WINAPI SaveWorker(LPVOID) {
    MediaReader fallback_artwork;
    while(workers_running.load(std::memory_order_acquire)) {
        SaveJob job;
        if(!saves.Pop(job)){WaitForSingleObject(save_event,200);continue;}
        std::string identity=Utf8(job.heard.media.artist)+" - "+Utf8(job.heard.media.title);
        try {
            // SMTC thumbnail streams can block. Keep this fallback on the save
            // worker and never attach artwork from a different current song.
            if(job.heard.media.cover.empty()) {
                Media artwork;
                if(fallback_artwork.Read(artwork,true) && artwork.Key()==job.heard.media.Key()) {
                    job.heard.media.cover=std::move(artwork.cover);
                    job.heard.media.cover_extension=std::move(artwork.cover_extension);
                }
            }
            auto result=Publish(*job.capture,job.heard,job.epoch);
            if(result==Publication::Saved) {
                auto settings=GetSettings();
                if(settings.capture_epoch==job.epoch) ++saved_count;
            }
        } catch(...) {LogActivity("failed",identity+" (publication exception)");}
        job.capture.reset();publishing_reserved.fetch_sub(job.reserved);
    }
    return 0;
}
struct CaptureBuffers {
    std::map<uintptr_t,std::unique_ptr<Capture>> active;
    std::vector<std::unique_ptr<Capture>> ready;
};
static void ObserveCodecQuality(const Slot& s,PlaybackQualityTracker& quality) {
    switch(s.kind) {
        case AudioEventKind::OggPage: quality.Ogg(s.context,s.bytes,s.length,s.time); break;
        case AudioEventKind::FlacBegin: quality.FlacBegin(s.context,s.time); break;
        case AudioEventKind::FlacBytes: quality.FlacBytes(s.context,s.bytes,s.length,size_t(s.input_length)); break;
        case AudioEventKind::FlacFrame: quality.FlacFrame(s.context,s.bytes,s.length); break;
        case AudioEventKind::FlacError: quality.FlacEnd(s.context,true); break;
        case AudioEventKind::FlacEnd: quality.FlacEnd(s.context,false); break;
        default:break;
    }
}
static void ProcessFlacCapture(const Slot& s,CaptureBuffers& buffers) {
    auto& active=buffers.active;auto& ready=buffers.ready;
    auto found=active.find(s.context);
    if(s.kind==AudioEventKind::FlacBegin) {
        auto c=std::make_unique<Capture>(); c->lossless=true; c->born=s.time;
        active[s.context]=std::move(c); Log("FLAC decoder initialized; capture begins at compressed offset zero"); return;
    }
    if(found==active.end() || !found->second->lossless) return;
    Capture& c=*found->second;
    if(s.kind==AudioEventKind::FlacError) { active.erase(found); Log("FLAC decoder error; stream discarded"); return; }
    if(s.kind==AudioEventKind::FlacBytes) {
        size_t reserved=publishing_reserved.load(); for(const auto& item:active) reserved+=item.second->data.capacity();
        for(const auto& item:ready) reserved+=item->data.capacity();
        size_t available=reserved<memory_limit ? memory_limit-reserved : 0;
        if(!c.data.Append(s.bytes,s.length,available)) { active.erase(found); Log("FLAC capture memory limit; stream discarded"); return; }
        c.bytes+=s.length;
        if(!c.metadata) {
            size_t audio=0; auto parsed=ParseFlacMetadata(c.data,c.flac,audio);
            if(parsed==FlacParse::Invalid) { active.erase(found); Log("FLAC metadata invalid; stream discarded"); return; }
            if(parsed==FlacParse::Valid) {
                c.metadata=true; char line[180]; snprintf(line,sizeof(line),"FLAC STREAMINFO rate=%u channels=%u bits=%u total=%llu metadata_bytes=%zu",c.flac.rate,c.flac.channels,c.flac.bits,static_cast<unsigned long long>(c.flac.total_samples),audio); Log(line);
            }
        }
        return;
    }
    if(s.kind==AudioEventKind::FlacFrame) {
        uint32_t frame[6]; uint64_t number; memcpy(frame,s.bytes,24); memcpy(&number,s.bytes+24,8);
        if(frame[5]==0) number=uint32_t(number);
        if(!c.metadata || !c.coverage.Frame(c.flac,frame[0],frame[1],frame[2],frame[4],frame[5],number)) {
            active.erase(found); Log("FLAC decoded-frame gap or format mismatch; stream discarded"); return;
        }
        if(!c.coverage.Complete(c.flac)) return;
    } else if(s.kind!=AudioEventKind::FlacEnd || !c.coverage.Complete(c.flac)) { active.erase(found); Log("FLAC truncated before total samples; discarded"); return; }
    c.finished=s.time;
    char line[180]; snprintf(line,sizeof(line),"FLAC COMPLETE frames=%u samples=%llu bytes=%llu duration=%.6f",c.coverage.frames,static_cast<unsigned long long>(c.coverage.samples),static_cast<unsigned long long>(c.bytes),c.Duration()); Log(line);
    ready.push_back(std::move(found->second)); active.erase(found); return;
}
static void ProcessOggCapture(const Slot& s,CaptureBuffers& buffers) {
    auto& active=buffers.active;auto& ready=buffers.ready;
    Page page; if(!ParsePage(s.bytes,s.length,page)) {
        active.erase(s.context); Log("invalid Ogg page rejected"); return;
    }
    if(page.vorbis_start) {
        auto c=std::make_unique<Capture>(); c->born=s.time;
        active[s.context]=std::move(c);
        char line[160]; snprintf(line,sizeof(line),"BOS ctx=%llx seq=%u serial=%u rate=%u channels=%u",
            static_cast<unsigned long long>(s.context),page.sequence,page.serial,page.rate,page.channels); Log(line);
    }
    auto found=active.find(s.context); if(found==active.end()) return;
    Capture& c=*found->second; Result result=c.stream.Push(s.bytes,s.length);
    if(result==Result::Invalid || result==Result::Ignore) {
        active.erase(found); Log("stream discarded: page gap or integrity failure"); return;
    }
    size_t reserved=publishing_reserved.load();
    for(const auto& item:active) reserved+=item.second->data.capacity();
    for(const auto& item:ready) reserved+=item->data.capacity();
    size_t available=reserved<memory_limit ? memory_limit-reserved : 0;
    if(!c.data.Append(s.bytes,s.length,available)) {
        active.erase(found);
        Log("capture memory limit reached; track discarded without scratch files"); return;
    }
    c.bytes+=s.length;
    if(result==Result::Complete) {
        c.finished=s.time;
        char line[200]; snprintf(line,sizeof(line),"EOS pages=%u bytes=%llu duration=%.6f capture_seconds=%.3f",
          c.stream.pages,static_cast<unsigned long long>(c.bytes),c.stream.Duration(),c.finished-c.born); Log(line);
        ready.push_back(std::move(found->second)); active.erase(found);
    }
}
static void ProcessCaptureEvent(const Slot& s,CaptureBuffers& buffers,PlaybackQualityTracker& quality,
                                bool observing_quality,bool enabled,unsigned epoch) {
    if(s.kind==AudioEventKind::FormatDetected) {
        char prefix[33]={};for(unsigned i=0;i<s.length;++i)snprintf(prefix+2*i,3,"%02x",s.bytes[i]);
        char line[180];snprintf(line,sizeof(line),"FORMAT kind=%u matched=%d input_bytes=%llu prefix=%s",
            s.format,s.matched,static_cast<unsigned long long>(s.input_length),prefix);Log(line);return;
    }
    if(observing_quality) ObserveCodecQuality(s,quality);
    if(!enabled || !s.capture || s.epoch!=epoch) return;
    if(s.kind>=AudioEventKind::FlacBegin) ProcessFlacCapture(s,buffers);
    else ProcessOggCapture(s,buffers);
}
static void AssociateListen(Media media,Listen& listen,Media& current,std::vector<Heard>& heard,
                            CaptureBuffers& buffers,double time) {
    auto& active=buffers.active;auto& ready=buffers.ready;
    double previous_start=listen.start_time;
    const auto previous_generation=listen.generation;
    bool was_eligible=listen.eligible;
    double prior_position=listen.last_position,prior_duration=listen.duration,prior_time=listen.last_time;
    std::string done=listen.Observe(media.Key(),media.position,media.duration,media.playing,time);
    if(was_eligible && !listen.eligible && done.empty()) {
        char line[300]; snprintf(line,sizeof(line),"listen invalidated old=%.6f/%.6f new=%.6f/%.6f delta=%.3f playing=%d",
            prior_position,prior_duration,media.position,media.duration,time-prior_time,media.playing); Log(line);
    }
    if(!done.empty()) { heard.push_back({current,listen.completed_start,listen.completed_time,listen.completed_generation}); Log(("FULL LISTEN "+Utf8(current.title)).c_str()); }
    if(previous_generation!=listen.generation && listen.eligible) LogActivity("started",Utf8(media.artist)+" - "+Utf8(media.title));
    if(was_eligible && !listen.eligible && done.empty()) LogActivity("failed",Utf8(current.artist)+" - "+Utf8(current.title)+" (incomplete listen)");
    if(previous_generation!=listen.generation) {
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
}
static bool CaptureMatchesListen(const Capture& capture,const Heard& heard) {
    return (!capture.listen_generation || capture.listen_generation==heard.generation) &&
           std::fabs(capture.Duration()-heard.media.duration)<=1.0 &&
           capture.born>=heard.start-20 && capture.born<=heard.start+3 && capture.born<heard.finish;
}
static void SchedulePublications(std::vector<Heard>& heard,CaptureBuffers& buffers,unsigned epoch,double now) {
    auto& ready=buffers.ready;
    for(auto h=heard.begin();h!=heard.end();) {
        size_t match=ready.size(),matches=0;
        for(size_t i=0;i<ready.size();i++) {
            Capture& c=*ready[i];
            if(CaptureMatchesListen(c,*h)) { match=i; ++matches; }
        }
        if(matches==1) {
            SaveJob job;job.reserved=ready[match]->data.capacity();job.epoch=epoch;
            job.capture=std::move(ready[match]);job.heard=*h;job.capture->listen_generation=h->generation;
            publishing_reserved.fetch_add(job.reserved);
            if(saves.Push(std::move(job)))SetEvent(save_event);
            else {size_t reserved=job.reserved;job.capture.reset();publishing_reserved.fetch_sub(reserved);LogActivity("failed","publication queue full; completed candidate discarded");}
            ready.erase(ready.begin()+match); h=heard.erase(h);
        } else if(matches>1 || now-h->finish>10) {
            Log("completed listen discarded: missing or ambiguous Ogg association"); h=heard.erase(h);
        } else ++h;
    }
}
static DWORD WINAPI Worker(LPVOID) {
    LONG overflow=0;
    CaptureBuffers buffers;auto& active=buffers.active;auto& ready=buffers.ready;
    std::vector<Heard> heard;
    Listen listen; MediaReader reader; Media current; PlaybackQualityTracker quality; std::string client_quality;
    double next_media=0,next_log=0;
    unsigned generation=~0u,epoch=~0u; bool enabled=false;
    try {
        while(workers_running.load(std::memory_order_acquire)) {
            double now=Now();
            auto preferences=GetSettings();
            if(preferences.generation!=generation) {
                bool initial=generation==~0u;
                generation=preferences.generation;
                output=preferences.root; memory_limit=size_t(preferences.max_buffered_mib)*1024*1024;
                bool capture=preferences.downloads && (preferences.ogg || preferences.flac);
                if(capture!=enabled || initial || preferences.capture_epoch!=epoch) {
                    active.clear(); ready.clear(); heard.clear(); listen={}; current={}; next_media=0;
                    if(!enabled && capture) saved_count=0;
                    enabled=capture;
                    Log(enabled ? "To Disk capture enabled" : "To Disk capture disabled");
                }
                epoch=preferences.capture_epoch;
            }
            if(!enabled && !quality_enabled.load(std::memory_order_relaxed)) {
                PublishPlaybackQuality({});
                PublishSongDestination(0,L"",{},false,0);
                Slot discard; while(Pop(discard)) {}
                WaitForSingleObject(event,200); continue;
            }
            if(dropped!=overflow) {
                overflow=dropped; active.clear(); ready.clear(); heard.clear(); listen.eligible=false; quality.ClearStreams();
                Log("capture queue overflow; streams and listening coverage invalidated");
            }
            Slot s;
            while(Pop(s)) ProcessCaptureEvent(s,buffers,quality,quality_enabled.load(std::memory_order_relaxed),enabled,epoch);
            if(!enabled) {
                if(now>=next_media) {
                    Media media;
                    if(reader.Read(media,false)) {PublishCurrentDestination(media,GetSettings());quality.Media(media.Key(),media.title,media.position,media.duration,Now(),media.playing);client_quality=ReadClientPlaybackQuality(media);}
                    else {PublishSongDestination(0,L"",{},false,0);quality.Media({},L"",0,0,Now());client_quality.clear();}
                    next_media=Now()+0.5;
                }
                PublishPlaybackQuality(quality.Snapshot(client_quality));
                WaitForSingleObject(event,50);continue;
            }
            if(now>=next_media) {
                Media media;
                if(reader.Read(media,false)) {
                    PublishCurrentDestination(media,GetSettings());
                    quality.Media(media.Key(),media.title,media.position,media.duration,Now(),media.playing);
                    client_quality=ReadClientPlaybackQuality(media);
                    AssociateListen(std::move(media),listen,current,heard,buffers,Now());
                } else { PublishSongDestination(0,L"",{},false,0);listen.eligible=false; quality.Media({},L"",0,0,Now()); Log("Spotify media snapshot unavailable; listen invalidated"); }
                next_media=Now()+0.5;
            }
            PublishPlaybackQuality(quality.Snapshot(client_quality));
            // A media-session read may block while the menu changes settings.
            // Apply that generation before considering any publication.
            if(GetSettings().generation!=generation) continue;
            SchedulePublications(heard,buffers,epoch,now);
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
            WaitForSingleObject(event,50);
        }
    } catch(...) { Log("history worker stopped after an exception; playback remains with original parser"); }
    PublishPlaybackQuality({});
    PublishSongDestination(0,L"",{},false,0);
    workers_running.store(false,std::memory_order_release);
    if(save_event) SetEvent(save_event);
    MH_DisableHook(target);
    MH_DisableHook(sniff_target);
    for(auto address:flac_targets) if(address) MH_DisableHook(address);
    return 0;
}
}

void StartAudioHistory(HMODULE spotify_module,HMODULE proxy) {
    const auto now=static_cast<std::uint64_t>(GetTickCount64());
    if(!audio_init.TryBegin(now)) return;
    (void)proxy;
    auto preferences=GetSettings(); output=preferences.root;
    quality_enabled.store(preferences.menu,std::memory_order_relaxed);
    StartSongDestinationWorker(preferences.menu);
    if(output.empty()) { Log("save location unavailable; history initialization will retry"); audio_init.Retry(now); return; }
    spotify::HookTargets discovered{};
    const auto discovery=DiscoverTargets(spotify_module,discovered);
    if(discovery!=spotify::DiscoveryResult::Found) {
        char message[220];snprintf(message,sizeof(message),
            "Spotify audio target discovery failed: %s; capture disabled",
            DiscoveryFailure(discovery));Log(message);
        audio_init.MarkUnsupported();return;
    }
    target=DiscoveredTarget(spotify_module,discovered,spotify::TargetKind::OggPageSeek);
    sniff_target=DiscoveredTarget(spotify_module,discovered,spotify::TargetKind::FormatSniff);
    flac_targets[0]=DiscoveredTarget(spotify_module,discovered,spotify::TargetKind::FlacInit);
    flac_targets[1]=DiscoveredTarget(spotify_module,discovered,spotify::TargetKind::FlacRead);
    flac_targets[2]=DiscoveredTarget(spotify_module,discovered,spotify::TargetKind::FlacFrame);
    flac_targets[3]=DiscoveredTarget(spotify_module,discovered,spotify::TargetKind::FlacError);
    auto release_resources=[] {
        if(event) { CloseHandle(event); event=nullptr; }
        if(save_event) { CloseHandle(save_event); save_event=nullptr; }
        if(queue) { HeapFree(GetProcessHeap(),0,queue); queue=nullptr; }
        head=tail=count=0;
        original=nullptr; original_sniff=nullptr; original_flac_init=nullptr;
        original_flac_read=nullptr; original_flac_frame=nullptr; original_flac_error=nullptr;
        target=nullptr; sniff_target=nullptr;
        for(auto& address:flac_targets) address=nullptr;
    };
    save_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    queue=static_cast<Slot*>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Slot)*capacity));
    event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!save_event || !queue || !event) {
        Log("audio queue resources unavailable; initialization will retry"); release_resources(); audio_init.Retry(now); return;
    }
    workers_running.store(true,std::memory_order_release);
    HANDLE publisher=CreateThread(nullptr,0,SaveWorker,nullptr,0,nullptr);
    if(!publisher) {
        workers_running.store(false,std::memory_order_release);
        Log("publication worker startup failed; initialization will retry"); release_resources(); audio_init.Retry(now); return;
    }
    SetThreadPriority(publisher,THREAD_PRIORITY_BELOW_NORMAL);
    HANDLE worker=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);
    if(!worker) {
        workers_running.store(false,std::memory_order_release); SetEvent(save_event);
        const bool stopped=WaitForSingleObject(publisher,5000)==WAIT_OBJECT_0;CloseHandle(publisher);
        if(stopped) {Log("audio worker startup failed; initialization will retry");release_resources();audio_init.Retry(now);}
        else {Log("audio worker startup failed and publisher did not quiesce; state preserved");audio_init.MarkUnsupported();}
        return;
    }
    auto close_threads=[&] {CloseHandle(publisher);CloseHandle(worker);publisher=nullptr;worker=nullptr;};
    void* created_targets[6]={};unsigned created_count=0;
    auto rollback=[&] {
        hooks::RollbackStatus result;
        auto disabled=[](MH_STATUS value){return value==MH_OK||value==MH_ERROR_DISABLED||value==MH_ERROR_NOT_CREATED;};
        auto removed=[](MH_STATUS value){return value==MH_OK||value==MH_ERROR_NOT_CREATED;};
        for(unsigned i=0;i<created_count;i++)result.ObserveDisable(disabled(MH_DisableHook(created_targets[i])));
        for(unsigned waited=0;audio_callbacks.Active()&&waited<5000;++waited)Sleep(1);
        result.ObserveQuiescence(audio_callbacks.Active()==0);
        if(result.quiescent)for(unsigned i=0;i<created_count;i++)result.ObserveRemove(removed(MH_RemoveHook(created_targets[i])));
        if(!result.CanRelease()){close_threads();return false;}
        workers_running.store(false,std::memory_order_release);SetEvent(event);SetEvent(save_event);
        const bool worker_stopped=WaitForSingleObject(worker,5000)==WAIT_OBJECT_0;
        const bool publisher_stopped=WaitForSingleObject(publisher,5000)==WAIT_OBJECT_0;
        close_threads();
        if(!worker_stopped||!publisher_stopped)return false;
        release_resources();return true;
    };
    auto create_hook=[&](void* address,void* callback,void** trampoline) {
        MH_STATUS result=MH_CreateHook(address,callback,trampoline);
        if(result==MH_OK)created_targets[created_count++]=address;
        return result;
    };
    MH_STATUS status=MH_Initialize(); if(status==MH_ERROR_ALREADY_INITIALIZED) status=MH_OK;
    if(status==MH_OK) status=create_hook(target,reinterpret_cast<void*>(Hook),reinterpret_cast<void**>(&original));
    if(status==MH_OK) status=create_hook(sniff_target,reinterpret_cast<void*>(FormatHook),reinterpret_cast<void**>(&original_sniff));
    void* flac_callbacks[]={reinterpret_cast<void*>(FlacInitHook),reinterpret_cast<void*>(FlacReadHook),reinterpret_cast<void*>(FlacFrameHook),reinterpret_cast<void*>(FlacErrorHook)};
    void** flac_originals[]={reinterpret_cast<void**>(&original_flac_init),reinterpret_cast<void**>(&original_flac_read),reinterpret_cast<void**>(&original_flac_frame),reinterpret_cast<void**>(&original_flac_error)};
    for(unsigned i=0;status==MH_OK && i<4;i++) {
        status=create_hook(flac_targets[i],flac_callbacks[i],flac_originals[i]);
        if(status==MH_OK) status=MH_QueueEnableHook(flac_targets[i]);
    }
    if(status==MH_OK) status=MH_QueueEnableHook(target);
    if(status==MH_OK) status=MH_QueueEnableHook(sniff_target);
    bool apply_attempted=false;
    if(status==MH_OK) {apply_attempted=true;status=MH_ApplyQueued();}
    if(status!=MH_OK) {
        char message[200];snprintf(message,sizeof(message),"audio hook installation failed: %s",MH_StatusToString(status));Log(message);
        if(apply_attempted) {
            // MH_ApplyQueued may have published some detours before reporting a
            // failure. A thread can be between the detour entry and our C++
            // callback counter, so keep every trampoline and callback resource
            // alive for the process lifetime after disabling the hooks.
            for(unsigned i=0;i<created_count;i++) MH_DisableHook(created_targets[i]);
            workers_running.store(false,std::memory_order_release);SetEvent(event);SetEvent(save_event);
            WaitForSingleObject(worker,5000);WaitForSingleObject(publisher,5000);close_threads();
            Log("audio hook activation failed; disabled hook state preserved for callback safety");
            audio_init.MarkUnsupported();
        } else if(rollback()){Log("audio hook rollback complete; retry scheduled");audio_init.Retry(now);}
        else {Log("audio hook rollback incomplete; callback state preserved and retries disabled");audio_init.MarkUnsupported();}
        return;
    }
    close_threads();
    audio_init.Activate();
    Log("native history active; dynamic Spotify targets; memory capture; embedded metadata/art; complete listens only");
}
