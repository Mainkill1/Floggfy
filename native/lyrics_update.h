#pragma once
#include "ogg_tags.h"
namespace history {
void PreserveFlacLyrics(const std::wstring& path,Tags& incoming);
enum class FileLyricsUpdate { Updated, Skipped, Failed };
// Fill missing lyrics or upgrade plain lyrics to LRC, staging a complete FLAC
// and atomically replacing
// the existing file. No decoding, encoding or artwork replacement occurs.
FileLyricsUpdate UpdateFlacLyrics(const std::wstring& path,const Tags& incoming);
}
