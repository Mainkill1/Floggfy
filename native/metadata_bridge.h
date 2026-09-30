#pragma once
#include <windows.h>
#include "media_session.h"
#include "ogg_tags.h"
namespace history {
void StartMetadataCollector(HMODULE cef);
void EnrichTags(const Media&,Tags&);
}
