#pragma once
#include <windows.h>
#include <string>
#include "recording_limits.h"
namespace history {
struct Settings {
    bool downloads=true,ogg=true,flac=true,aac=false,podcasts=true,menu=true,music_folder=false;
    bool windows_playback_fallback=false;
    bool metadata=true,lyrics=true,update_existing_metadata=true,auto_skip_downloaded=false;
    int never_pause=-1; // -1 hides/disables the menu item; 0=off, 1=on.
    bool track_number_in_filename=false,convert_numbered_filenames=false;
    bool debug_hook_diagnostics=false; // Extra bounded activity/stall diagnostics, restart-only.
    bool debug_networking=false; // Master deep-diagnostics hook + separate log; restart-only.
    bool debug_connect=true; // Requires DebugLog; observes cached Connect state/local actions.
    bool network_patch=false; // INI-only startup opt-in; requires a full restart.
    unsigned max_buffered_mib=kDefaultBufferedMiB,capture_speed=1,capture_speed_limit=0,log_level=1,generation=0,capture_epoch=0;
    std::wstring root,save_location;
};
void InitSettings(HMODULE proxy);
Settings GetSettings();
bool SetDownloads(bool enabled);
bool SetOgg(bool enabled);
bool SetFlac(bool enabled);
bool SetPodcasts(bool enabled);
bool SetWindowsPlaybackFallback(bool enabled);
bool SetLyrics(bool enabled);
bool SetAutoSkipDownloaded(bool enabled);
bool SetNeverPause(bool enabled);
// INI-only user preference; changing optional enrichment never resets capture.
bool SetUpdateExistingMetadata(bool enabled);
bool SetSaveLocation(const std::wstring& root);
bool OggEnabled();
bool FlacEnabled();
bool AACEnabled();
bool PodcastsEnabled();
unsigned CaptureEpoch();
unsigned CaptureSpeed();
unsigned CaptureSpeedLimit();
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
