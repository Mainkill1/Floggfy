#pragma once
#include <windows.h>
#include <string>
namespace startup {
// Returned handles belong to the caller. Only a newly created process is
// modified; failed launches are terminated before application entry resumes.
bool Launch(const std::wstring& folder,PROCESS_INFORMATION& process,std::wstring& error);
}
