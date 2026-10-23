#pragma once
#include <windows.h>
#include <string>
namespace history {
struct Settings {
    bool downloads=true,ogg=true,flac=true,menu=true,music_folder=false;
    bool metadata=true,lyrics=true,update_existing_metadata=true,log=true,debug_log=false;
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
unsigned CaptureEpoch();
bool EnsureDirectory(const std::wstring& path);
std::wstring WindowsPath(const std::wstring& path);
void PickSaveLocation(HWND owner);
bool LoggingEnabled();
bool DebugLoggingEnabled();
bool FlushSettings(unsigned timeout_ms);
void HistoryLog(const char* message);
}
