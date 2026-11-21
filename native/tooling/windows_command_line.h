#pragma once
#include <string>
#include <string_view>

namespace win32::command_line {
inline bool Space(wchar_t character) {return character==L' '||character==L'\t';}
inline std::wstring Executable(std::wstring_view command) {
  size_t index=0;while(index<command.size()&&Space(command[index]))++index;
  std::wstring result;bool quoted=false;
  for(;index<command.size();++index) {
    if(command[index]==L'"'){quoted=!quoted;continue;}
    if(!quoted&&Space(command[index]))break;
    result+=command[index];
  }
  return quoted?std::wstring{}:result;
}
}
