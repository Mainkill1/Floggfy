#pragma once
#include <windows.h>
#include <cstddef>

namespace history {
inline bool WriteAllBytes(HANDLE file,const void* bytes,std::size_t size) {
    auto data=static_cast<const char*>(bytes);
    while(size) {
        const DWORD chunk=size>MAXDWORD?MAXDWORD:static_cast<DWORD>(size);
        DWORD written=0;
        if(!WriteFile(file,data,chunk,&written,nullptr)||!written)return false;
        data+=written;
        size-=written;
    }
    return true;
}
}
