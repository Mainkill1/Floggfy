#pragma once
#include <windows.h>
#include <string>
#include <cstdint>
#include "../media/recording_limits.h"
namespace history {
struct Settings {
    bool downloads=true,ogg=true,flac=true,aac=false,podcasts=true,menu=true,music_folder=false;
    bool read_spotify_disk_cache=true;
    bool fetch_missing_metadata=true;
    bool fetch_missing_artwork=true;
    bool fetch_missing_lyrics=true;
    bool windows_playback_fallback=false;
    bool metadata=true,lyrics=true,update_existing_metadata=true,auto_skip_downloaded=false;
    unsigned metadata_cache_read_mib=256; // Disk read budget per cached metadata lookup.
    int never_pause=-1; // -1 hides/disables the menu item; 0=off, 1=on.
    bool track_number_in_filename=false,convert_numbered_filenames=false;
    bool dump_streams=false; // Continuous raw input and metadata files; restart-only.
    bool save_unmatched=true; // Save complete unidentified recordings after matcher_max_queue new starts.
    unsigned matcher_max_queue=5,matcher_time_offset_ms=10000,matcher_duration_ms=25;
    bool debug_hook_diagnostics=false; // Extra bounded activity/stall diagnostics, restart-only.
    bool debug_networking=false; // Master deep-diagnostics hook + separate log; restart-only.
    bool debug_connect=false; // Requires LogLevel=4; observes cached Connect state/local actions.
    bool block_telemetry=false; // Restart-only privacy opt-in; independent of capture/enrichment.
    bool block_event_reports=true,block_playback_reports=true,block_growth_analytics=true,block_sentry=true;
    bool block_crash_reports=true,block_internal_logging=true,block_track_errors=true,block_ai_telemetry=true;
    bool block_queued_telemetry=true;
    bool dump_telemetry=false; // Restart-only opt-in readable blocked-request log.
    bool block_updates=false; // Local Update-folder access policy; independent of capture.
    bool marked_when_downloaded=true; // Local saved-date index and playlist column.
    bool network_patch=false; // INI-only startup opt-in; requires a full restart.
    int capture_speed=-1; // -1 hides the menu; 0 and 1 both capture at normal speed.
    int network_kill_switch=0; // -1 hides/disables; 0=off, 1=on.
    unsigned max_buffered_mib=kDefaultBufferedMiB,log_level=1,generation=0,capture_epoch=0;
    // Scoped publication revisions exclude pacing and other playback preferences.
    uint64_t artifact_revision=0,destination_revision=0;
    std::wstring root,save_location;
};
void InitSettings(HMODULE proxy);
Settings GetSettings();
bool SetDownloads(bool enabled);
bool SetOgg(bool enabled);
bool SetAAC(bool enabled);
bool SetFlac(bool enabled);
bool SetPodcasts(bool enabled);
unsigned EnrichmentEnableRevision();
bool SetReadSpotifyDiskCache(bool enabled);
bool SetFetchMissingMetadata(bool enabled);
bool SetFetchMissingArtwork(bool enabled);
bool SetFetchMissingLyrics(bool enabled);
bool SetWindowsPlaybackFallback(bool enabled);
bool SetLyrics(bool enabled);
bool SetAutoSkipDownloaded(bool enabled);
bool SetNeverPause(bool enabled);
// Caller applies networking enforcement before persisting this preference.
bool SetNetworkKillSwitch(bool enabled);
// Caller applies the Update-folder policy before persisting this preference.
bool SetBlockUpdates(bool enabled);
bool SetMarkedWhenDownloaded(bool enabled);
// INI-only user preference; changing optional enrichment never resets capture.
bool SetUpdateExistingMetadata(bool enabled);
bool SetSaveLocation(const std::wstring& root);
bool OggEnabled();
bool FlacEnabled();
bool AACEnabled();
bool DownloadsEnabled();
unsigned CaptureEpoch();
unsigned CaptureSpeed();
bool CaptureSpeedAvailable(unsigned speed);
bool SetCaptureSpeed(unsigned speed);
bool CaptureAccelerationAvailable();
void SetCaptureAccelerationAvailable(bool available);
bool EnsureDirectory(const std::wstring& path);
std::wstring WindowsPath(const std::wstring& path);
void PickSaveLocation(HWND owner);
bool LoggingEnabled();
unsigned LogLevel();
bool DebugLoggingEnabled();
bool ConnectLoggingEnabled();
bool NetworkingLoggingEnabled();
bool HookDiagnosticsEnabled();
bool FlushSettings(unsigned timeout_ms);
}
