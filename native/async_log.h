#pragma once
#include <string>
namespace history {
void StartLogger();
void LogActivity(const char* event,const std::string& detail);
void QueueDiagnostic(const char* message);
bool WriteCappedLog(const std::wstring& path,const std::string& line);
}
