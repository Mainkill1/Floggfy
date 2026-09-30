#pragma once
#include <windows.h>
#include <string>
namespace history {
struct Settings {
    bool downloads=false,ogg=true,flac=true,menu=true,music_folder=false;
    bool metadata=true,log=true,debug_log=false;
    unsigned max_buffered_mib=64,stop_after=0,generation=0,capture_epoch=0;
    std::wstring root,save_location;
};
void InitSettings(HMODULE proxy);
Settings GetSettings();
bool SetDownloads(bool enabled);
bool SetOgg(bool enabled);
bool SetFlac(bool enabled);
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
