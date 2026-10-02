#pragma once
#include <windows.h>
#include <cwchar>
namespace startup {
inline void ReadyName(DWORD pid,wchar_t (&name)[80]) {
  swprintf(name,80,L"Local\\FloggfyStartupReady-%lu",pid);
}
// Created outside DllMain and retained for the process lifetime so a launcher
// can observe readiness even when the normal adjacent-DLL path already worked.
inline void SignalReady() {
  wchar_t name[80]{};ReadyName(GetCurrentProcessId(),name);
  static HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,name);
  if(ready)SetEvent(ready);
}
}
