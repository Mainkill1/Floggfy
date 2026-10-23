#pragma once
#include "library_layout.h"
#include <cstdint>
#include <string>
namespace history {
// Worker-side only: uses the publication path builder, without filesystem I/O.
void PublishSongDestination(std::uint64_t identity,const std::wstring& root,
                            const Catalog& catalog,bool music_folder,unsigned settings_generation);
// Never waits for a publisher; only returns a fresh destination for this song and settings generation.
// Failure leaves folder unchanged. Menu callers must disable unmatched targets.
bool ReadSongDestination(std::uint64_t identity,unsigned settings_generation,std::wstring& folder);
}
