#pragma once
#include <string>
#include <string_view>
namespace history {
void StartLogger();
void LogActivity(const char* event,const std::string& detail);
void QueueDiagnostic(const char* message);
// Claims all reserved renderer diagnostics, including rejected/disabled input.
// False means an unrelated console message; never forwards raw renderer text.
bool QueueRendererDiagnostic(std::wstring_view message) noexcept;
// Trusted, constructed summaries only; renderer IPC must pass the field allowlist.
// Never pass bodies, URLs, headers, credentials or media to this function.
void QueueNetworkingDiagnostic(const char* message) noexcept;
bool WriteCappedLog(const std::wstring& path,const std::string& line);
}
