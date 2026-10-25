#pragma once
#include <windows.h>
#include <string>
namespace history {
struct Settings {
    bool downloads=true,ogg=true,flac=true,aac=false,menu=true,music_folder=false;
    bool metadata=true,lyrics=true,update_existing_metadata=true,log=true,debug_log=false;
    bool track_number_in_filename=false,convert_numbered_filenames=false;
    bool debug_networking=false; // Master deep-diagnostics hook + separate log; restart-only.
    bool debug_connect=true; // Requires DebugLog; observes cached Connect state/local actions.
    bool network_patch=false; // INI-only startup opt-in; requires a full restart.
    unsigned max_buffered_mib=500,generation=0,capture_epoch=0;
    std::wstring root,save_location;
};
void InitSettings(HMODULE proxy);
Settings GetSettings();
bool SetDownloads(bool enabled);
bool SetOgg(bool enabled);
bool SetFlac(bool enabled);
bool SetLyrics(bool enabled);
// INI-only user preference; changing optional enrichment never resets capture.
bool SetUpdateExistingMetadata(bool enabled);
bool SetSaveLocation(const std::wstring& root);
bool OggEnabled();
bool FlacEnabled();
bool AACEnabled();
unsigned CaptureEpoch();
bool EnsureDirectory(const std::wstring& path);
std::wstring WindowsPath(const std::wstring& path);
void PickSaveLocation(HWND owner);
bool LoggingEnabled();
bool DebugLoggingEnabled();
bool NetworkingLoggingEnabled();
bool FlushSettings(unsigned timeout_ms);
void HistoryLog(const char* message);
}
