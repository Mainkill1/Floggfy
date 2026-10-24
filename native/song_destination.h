#pragma once
#include "library_layout.h"
#include <cstdint>
#include <string>
namespace history {
// Initialize once at startup. Menu=0 leaves this worker disabled and creates no
// thread. Disabling also invalidates queued work and any in-flight reply.
void StartSongDestinationWorker(bool enabled) noexcept;
// RAM-only request submission, safe for capture/heartbeat threads. One worker
// probes storage; at most one superseding request is retained while it blocks.
void PublishSongDestination(std::uint64_t identity,const std::wstring& root,
                            const Catalog& catalog,bool music_folder,unsigned settings_generation,bool track_numbers=false) noexcept;
// Successful publication requests a fresh probe without touching storage here.
void RefreshSongDestination() noexcept;
// Nonblocking RAM snapshot read, matching latest identity/settings/freshness.
// Failure leaves saved_file unchanged. Menu callers must disable unmatched rows.
bool ReadSongDestination(std::uint64_t identity,unsigned settings_generation,std::wstring& saved_file);
}
