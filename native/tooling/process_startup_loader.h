#pragma once
#include <windows.h>
#include <string>

namespace startup {
// Drive a process created with DEBUG_ONLY_THIS_PROCESS to its application
// entry point, detach the debugger, and leave its primary thread suspended.
bool StopAtEntry(PROCESS_INFORMATION& process) noexcept;

// Load and verify an exact DLL path in a process already stopped at entry.
bool LoadStoppedProcess(PROCESS_INFORMATION& process,const std::wstring& dll,std::wstring& error) noexcept;

// Create a child under the debugger, load and initialize the DLL before entry,
// then restore the caller's requested running or suspended state.
using CreateProcessFunction=decltype(&CreateProcessW);
BOOL CreateWithDll(CreateProcessFunction original,LPCWSTR application,LPWSTR command,
 LPSECURITY_ATTRIBUTES process_attributes,LPSECURITY_ATTRIBUTES thread_attributes,
 BOOL inherit,DWORD flags,LPVOID environment,LPCWSTR directory,
 LPSTARTUPINFOW startup,LPPROCESS_INFORMATION process,const std::wstring& dll) noexcept;
}
