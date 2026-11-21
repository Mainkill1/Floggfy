#pragma once
#include <windows.h>
#include <string>

namespace startup {
// Drive a process created with DEBUG_ONLY_THIS_PROCESS to its application
// entry point, detach the debugger, and leave its primary thread suspended.
bool StopAtEntry(PROCESS_INFORMATION& process) noexcept;

// Load and verify an exact DLL path in a process already stopped at entry.
bool LoadStoppedProcess(PROCESS_INFORMATION& process,const std::wstring& dll,std::wstring& error) noexcept;
}
