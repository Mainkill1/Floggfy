#pragma once
#include <windows.h>
#include "media_session.h"
#include "ogg_tags.h"
namespace history {
// Prepare creates/queues root hooks without suspension. Finalize follows the
// shared ApplyQueued result; failure may retain dependent callback state.
bool PrepareMetadataCollector(HMODULE cef);
void FinalizeMetadataCollector(bool applied);
void StartMetadataCollector(HMODULE cef);
void EnrichTags(const Media&,Tags&);
std::string ReadClientPlaybackQuality(const Media&);
}
