#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
namespace history {
constexpr std::size_t max_connect_message=16384;
enum class ConnectAction { Pause,Resume,Play,Seek,SkipNext,SkipPrevious,Transfer,Other };
struct ConnectMessage {
    ConnectAction action=ConnectAction::Other;
    bool has_message_id=false;
    std::uint32_t message_id=0;
    std::string sender; // Private, bounded correlation input; never print/persist.
};
// Extract an allowlisted summary of an already decoded JSON request. Bounded
// depth/length; ignores unrelated content and rejects ambiguous selected keys.
// This does not establish sender authenticity or successful command execution.
bool SummarizeConnectMessage(std::string_view json,ConnectMessage& out) noexcept;
const char* ConnectActionName(ConnectAction action) noexcept;
}
