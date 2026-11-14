#pragma once
#include <windows.h>
#include <cstdint>
#include "media_session.h"
#include "ogg_tags.h"
#include "library_layout.h"
namespace history {
// Prepare creates/queues root hooks without suspension. Finalize follows the
// shared ApplyQueued result; failure may retain dependent callback state.
bool PrepareMetadataCollector(HMODULE cef);
void FinalizeMetadataCollector(bool applied);
// RAM-only identity enrichment; never reads storage or fetches artwork.
void EnrichCatalog(const Media&,Catalog&);
void EnrichTags(const Media&,Tags&);
// Includes immediately visible artwork content and cache availability changes.
std::uint64_t MetadataRevision();
std::string ReadClientPlaybackQuality(const Media&);
}
