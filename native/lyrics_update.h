#pragma once
#include "ogg_tags.h"
#include "library_layout.h"
namespace history {
void PreserveFlacLyrics(const std::wstring& path,Tags& incoming,const Catalog& catalog);
enum class FileLyricsUpdate { Updated, Skipped, Failed };
// Fill missing lyrics or upgrade plain lyrics to LRC using guarded publication.
// Keep the verified complete owned recording locked through replacement; never
// overwrite a foreign destination appearing while the old object is reserved.
FileLyricsUpdate UpdateFlacLyrics(const std::wstring& path,const Tags& incoming,const Catalog& catalog);
}
