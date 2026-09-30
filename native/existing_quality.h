#pragma once
#include "library_layout.h"
#include <string>
namespace history {
bool ReadQuality(const std::wstring& file,Quality& out);
enum class SaveDecision { NewFile, Upgrade, Skip };
SaveDecision DecideSave(const std::wstring& path,const Quality& incoming);
}
