#pragma once
#include <windows.h>
#include <string>
namespace startup {
bool StopExisting(const std::wstring& exe,std::wstring& error);
// Returned handles belong to the caller. Existing processes from this exact
// Spotify installation are stopped before starting the new process.
bool Launch(const std::wstring& folder,PROCESS_INFORMATION& process,std::wstring& error);
}
