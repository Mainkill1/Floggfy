#define WIN32_LEAN_AND_MEAN
#include "startup_launcher.h"
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
  wchar_t path[32768]{};
  auto length=GetModuleFileNameW(nullptr,path,32768);
  if(!length || length>=32768)return 1;
  std::wstring folder(path,length);auto slash=folder.find_last_of(L'\\');
  if(slash==std::wstring::npos)return 1;
  folder.resize(slash);
  PROCESS_INFORMATION process{};std::wstring error;
  try {
    if(!startup::Launch(folder,process,error)) {
      MessageBoxW(nullptr,error.c_str(),L"Floggfy",MB_OK|MB_ICONERROR);return 1;
    }
  }catch(...) {MessageBoxW(nullptr,L"Floggfy could not start Spotify.",L"Floggfy",MB_OK|MB_ICONERROR);return 1;}
  CloseHandle(process.hThread);CloseHandle(process.hProcess);return 0;
}
