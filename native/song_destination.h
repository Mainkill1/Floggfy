#pragma once
#include "library_layout.h"
#include <cstdint>
#include <string>
namespace history {
// Worker-side only: probes exact saver FLAC/OGG paths, preferring FLAC.
// Never creates directories or files.
void PublishSongDestination(std::uint64_t identity,const std::wstring& root,
                            const Catalog& catalog,bool music_folder,unsigned settings_generation);
// RAM-only, never waits for a publisher; returns an existing saved audio path
// from a fresh snapshot matching this song and settings generation.
// Failure leaves saved_file unchanged. Menu callers must disable unmatched targets.
bool ReadSongDestination(std::uint64_t identity,unsigned settings_generation,std::wstring& saved_file);
}
