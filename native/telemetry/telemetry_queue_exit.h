#pragma once
#include <string>
namespace privacy {
// Main Spotify process only, restart-only. Runs before normal ExitProcess;
// forced termination and writes during later DLL teardown are not covered.
bool InstallQueueExitCleanup(bool enabled,const std::wstring& users_root,
    const std::wstring& log_path,unsigned log_level) noexcept;
bool InstallRoamingQueueExitCleanup(bool enabled,const std::wstring& log_path,unsigned log_level) noexcept;
}
