#pragma once
#include <windows.h>
#include <climits>
#include <string>
#include <string_view>

namespace win32 {
// UTF-8 has at most one UTF-16 code unit per input byte. Decode directly
// into the final buffer and preserve its prefix on failure.
inline bool AppendUtf8(std::wstring& out,std::string_view text) {
 if(text.empty())return true;
 if(text.size()>INT_MAX||text.size()>out.max_size()-out.size())return false;
 const auto start=out.size();
 out.resize(start+text.size());
 const int written=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),
                                       out.data()+start,int(text.size()));
 out.resize(start+written);
 return written!=0;
}
}
