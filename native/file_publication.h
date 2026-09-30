#pragma once
#include "existing_quality.h"
#include <cstddef>
namespace history {
enum class FilePublication { Saved, Upgraded, Skipped, Failed };
// Input is an already complete, validated and tagged file held in memory.
FilePublication PublishBytes(const std::wstring& path,const Quality& quality,const void* bytes,size_t size);
}
