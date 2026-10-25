#pragma once
#include <string>
namespace history {
void StartLogger();
void LogActivity(const char* event,const std::string& detail);
void QueueDiagnostic(const char* message);
// Trusted, constructed summaries only; renderer IPC must pass the field allowlist.
// Never pass bodies, URLs, headers, credentials or media to this function.
void QueueNetworkingDiagnostic(const char* message) noexcept;
bool WriteCappedLog(const std::wstring& path,const std::string& line);
}
