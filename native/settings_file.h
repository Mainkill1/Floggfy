#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace history { namespace settings_file {
inline constexpr size_t max_text_units=1024*1024;
// Preserve the complete INI text, including unknown sections and comments.
// A single flushed replacement publishes all preference changes together.
struct Handle {
    HANDLE value;
    explicit Handle(HANDLE h):value(h){}
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
};
inline bool Read(const std::wstring& path,std::wstring& text,bool& unicode) {
    Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));
    if(file.value==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file.value,&size)||size.QuadPart<0||size.QuadPart>static_cast<LONGLONG>(2*max_text_units+2))return false;
    std::vector<unsigned char> bytes(size_t(size.QuadPart));DWORD got=0;
    if(!ReadFile(file.value,bytes.data(),DWORD(bytes.size()),&got,nullptr)||got!=bytes.size())return false;
    unicode=bytes.size()>=2&&bytes[0]==0xff&&bytes[1]==0xfe;
    if(unicode) {
        if(bytes.size()%2)return false;
        text.resize((bytes.size()-2)/2);
        if(!text.empty())std::memcpy(text.data(),bytes.data()+2,text.size()*2);
    } else {
        const bool utf8=bytes.size()>=3&&bytes[0]==0xef&&bytes[1]==0xbb&&bytes[2]==0xbf;
        const size_t skip=utf8?3:0;const auto count=int(bytes.size()-skip);
        if(!count){text.clear();return true;}
        const auto source=reinterpret_cast<const char*>(bytes.data()+skip);
        const auto page=utf8?CP_UTF8:CP_ACP;
        const int length=MultiByteToWideChar(page,MB_ERR_INVALID_CHARS,source,count,nullptr,0);
        if(!length)return false;
        text.resize(size_t(length));
        if(MultiByteToWideChar(page,MB_ERR_INVALID_CHARS,source,count,text.data(),length)!=length)return false;
    }
    if(text.size()>max_text_units)return false;
    // Embedded NULs and malformed UTF-16 cannot round-trip through the profile API.
    for(size_t i=0;i<text.size();++i) {
        const auto c=text[i];if(!c)return false;
        if(c>=0xd800&&c<=0xdbff){if(++i==text.size()||text[i]<0xdc00||text[i]>0xdfff)return false;}
        else if(c>=0xdc00&&c<=0xdfff)return false;
    }
    return true;
}
inline std::vector<unsigned char> Encode(const std::wstring& text) {
    std::vector<unsigned char> bytes(2+text.size()*2);bytes[0]=0xff;bytes[1]=0xfe;
    if(!text.empty())std::memcpy(bytes.data()+2,text.data(),text.size()*2);
    return bytes;
}
inline bool Write(const std::wstring& path,const std::wstring& text,bool create=false) {
    if(text.size()>max_text_units)return false;
    const auto bytes=Encode(text);
    static volatile LONG sequence=0;
    const auto staged=path+L".settings-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(InterlockedIncrement(&sequence));
    bool ok=false;
    {
        Handle file(CreateFileW(staged.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
        if(file.value==INVALID_HANDLE_VALUE)return false;
        DWORD written=0;ok=WriteFile(file.value,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file.value);
    }
    if(ok)ok=create ? MoveFileExW(staged.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)!=0 :
                     ReplaceFileW(path.c_str(),staged.c_str(),nullptr,0,nullptr,nullptr)!=0;
    if(!ok)DeleteFileW(staged.c_str());
    else WritePrivateProfileStringW(nullptr,nullptr,nullptr,path.c_str());
    return ok;
}
inline bool Unicode(const std::wstring& path) {
    std::wstring text;bool unicode=false;
    return Read(path,text,unicode)&&(unicode||Write(path,text));
}
inline std::wstring Trim(std::wstring text) {
    const auto first=text.find_first_not_of(L" \t\r");
    if(first==std::wstring::npos)return {};
    return text.substr(first,text.find_last_not_of(L" \t\r")-first+1);
}
inline void Set(std::wstring& text,const wchar_t* key,const std::wstring& value) {
    bool inside=false,found_section=false,found_key=false;
    size_t insertion=text.size();
    for(size_t p=0;p<text.size();) {
        const auto end=text.find(L'\n',p);const auto next=end==std::wstring::npos?text.size():end+1;
        const auto line=Trim(text.substr(p,(end==std::wstring::npos?text.size():end)-p));
        if(!line.empty()&&line.front()==L'['&&line.back()==L']') {
            if(inside)insertion=p;
            inside=_wcsicmp(line.c_str(),L"[To Disk]")==0;
            if(inside){found_section=true;insertion=next;}
        }else if(inside) {
            insertion=next;
            const auto equal=line.find(L'=');
            if(equal!=std::wstring::npos&&_wcsicmp(Trim(line.substr(0,equal)).c_str(),key)==0) {
                const auto separator=text.find(L'=',p);
                auto finish=end==std::wstring::npos?text.size():end;
                if(finish>separator&&text[finish-1]==L'\r')--finish;
                text.replace(separator+1,finish-separator-1,value);
                found_key=true;
                p=separator+1+value.size();
                const auto newline=text.find(L'\n',p);p=newline==std::wstring::npos?text.size():newline+1;
                continue;
            }
        }
        p=next;
    }
    if(found_key)return;
    if(!found_section) {
        if(!text.empty()&&text.back()!=L'\n')text+=L"\r\n";
        text+=L"[To Disk]\r\n";insertion=text.size();
    }
    if(insertion&&text[insertion-1]!=L'\n') {text.insert(insertion,L"\r\n");insertion+=2;}
    text.insert(insertion,std::wstring(key)+L"="+value+L"\r\n");
}
}}
