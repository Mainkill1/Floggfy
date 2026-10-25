#pragma once
#include <windows.h>
#include <array>
#include <cstring>
#include <string>

namespace startup {
enum class Architecture {X64,X86,Arm64,Other,Invalid};
struct ArchitectureInspection {Architecture architecture=Architecture::Invalid;DWORD error=0;};
// Header inspection only, never LoadLibrary. Bound the offset/read regardless
// of file-controlled lengths, and keep read errors distinct from architecture.
inline ArchitectureInspection InspectArchitecture(const std::wstring& path) {
    struct File {HANDLE value;~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
    File file{CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    if(file.value==INVALID_HANDLE_VALUE)return {Architecture::Invalid,GetLastError()};
    auto read=[&](void* data,DWORD length) {
        DWORD got=0;
        if(!ReadFile(file.value,data,length,&got,nullptr))return GetLastError();
        return got==length?DWORD(ERROR_SUCCESS):DWORD(ERROR_BAD_EXE_FORMAT);
    };
    IMAGE_DOS_HEADER dos{};auto error=read(&dos,sizeof(dos));
    if(error)return {Architecture::Invalid,error};
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<LONG(sizeof(dos))||dos.e_lfanew>1024*1024)
        return {Architecture::Invalid,ERROR_BAD_EXE_FORMAT};
    LARGE_INTEGER offset{};offset.QuadPart=dos.e_lfanew;
    if(!SetFilePointerEx(file.value,offset,nullptr,FILE_BEGIN))return {Architecture::Invalid,GetLastError()};
    DWORD signature=0;IMAGE_FILE_HEADER header{};WORD magic=0;
    if((error=read(&signature,sizeof(signature)))||(error=read(&header,sizeof(header)))||(error=read(&magic,sizeof(magic))))
        return {Architecture::Invalid,error};
    const DWORD required=magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC?sizeof(IMAGE_OPTIONAL_HEADER64):
        magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC?sizeof(IMAGE_OPTIONAL_HEADER32):0;
    if(signature!=IMAGE_NT_SIGNATURE||!required||header.SizeOfOptionalHeader<required||
       header.SizeOfOptionalHeader>4096||!header.NumberOfSections||header.NumberOfSections>96)
        return {Architecture::Invalid,ERROR_BAD_EXE_FORMAT};
    std::array<unsigned char,sizeof(IMAGE_OPTIONAL_HEADER64)> optional{};
    std::memcpy(optional.data(),&magic,sizeof(magic));
    if((error=read(optional.data()+sizeof(magic),required-sizeof(magic))))return {Architecture::Invalid,error};
    LARGE_INTEGER length{};
    if(!GetFileSizeEx(file.value,&length))return {Architecture::Invalid,GetLastError()};
    const auto header_end=static_cast<ULONGLONG>(dos.e_lfanew)+sizeof(signature)+sizeof(header)+
        header.SizeOfOptionalHeader+ULONGLONG(header.NumberOfSections)*sizeof(IMAGE_SECTION_HEADER);
    if(length.QuadPart<0||header_end>static_cast<ULONGLONG>(length.QuadPart))
        return {Architecture::Invalid,ERROR_BAD_EXE_FORMAT};
    if(header.Machine==IMAGE_FILE_MACHINE_AMD64&&magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC)return {Architecture::X64,0};
    if(header.Machine==IMAGE_FILE_MACHINE_I386&&magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC)return {Architecture::X86,0};
    if(header.Machine==IMAGE_FILE_MACHINE_ARM64&&magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC)return {Architecture::Arm64,0};
    return {Architecture::Other,ERROR_BAD_EXE_FORMAT};
}
inline bool RequireX64(const std::wstring& path,const wchar_t* name,std::wstring& error) {
    const auto result=InspectArchitecture(path);
    if(result.architecture==Architecture::X64)return true;
    if(result.architecture==Architecture::Invalid) {
        error=std::wstring(L"Cannot inspect ")+name+L" (Windows error "+std::to_wstring(result.error)+L").";
    }else {
        const auto* architecture=result.architecture==Architecture::X86?L"32-bit (x86)":
            result.architecture==Architecture::Arm64?L"ARM64":L"an unsupported architecture";
        error=std::wstring(name)+L" is "+architecture+L". Floggfy requires x64 Spotify and x64 Floggfy.dll. "
            L"The x64 build supports both Intel and AMD processors; no separate C++ Redistributable is required.";
    }
    return false;
}
}
