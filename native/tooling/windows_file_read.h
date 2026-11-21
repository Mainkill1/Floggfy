#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace win32 {
// Read an already sized file. The caller supplies its size limit and sharing mode.
inline bool ReadFileBytes(HANDLE file,size_t size,std::vector<uint8_t>& bytes) {
    if(!SetFilePointerEx(file,LARGE_INTEGER{},nullptr,FILE_BEGIN))return false;
    bytes.resize(size);
    constexpr size_t chunk=1024*1024;
    for(size_t offset=0;offset<size;) {
        const auto wanted=DWORD(std::min(chunk,size-offset));
        DWORD read=0;
        if(!ReadFile(file,bytes.data()+offset,wanted,&read,nullptr)||read!=wanted)return false;
        offset+=read;
    }
    LARGE_INTEGER after{};
    return GetFileSizeEx(file,&after)&&after.QuadPart>=0&&uint64_t(after.QuadPart)==size;
}
}
