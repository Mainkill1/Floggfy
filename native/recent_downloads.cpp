#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "recent_downloads.h"
#include <algorithm>
#include <utility>

namespace history { namespace {
constexpr size_t max_title=1024, max_path=32767;
SRWLOCK lock=SRWLOCK_INIT;
std::vector<RecentDownload> rows;
struct Exclusive {
    Exclusive(){AcquireSRWLockExclusive(&lock);}
    ~Exclusive(){ReleaseSRWLockExclusive(&lock);}
};
bool SamePath(const std::wstring& a,const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(),int(a.size()),b.c_str(),int(b.size()),TRUE)==CSTR_EQUAL;
}
bool Clean(RecentDownload& row) {
    if(row.path.empty()||row.path.size()>max_path)return false;
    for(auto& c:row.path) {if(c<32||c==127)return false;if(c==L'/')c=L'\\';}
    const auto& p=row.path;
    const bool drive=p.size()>3&&((p[0]>=L'A'&&p[0]<=L'Z')||(p[0]>=L'a'&&p[0]<=L'z'))&&p[1]==L':'&&p[2]==L'\\';
    const bool unc=p.size()>4&&p[0]==L'\\'&&p[1]==L'\\'&&p.find(L'\\',2)!=std::wstring::npos;
    if(!drive&&!unc)return false;
    // GetFullPathName only normalizes the string; it does not access the file.
    std::wstring full(max_path+1,L'\0');
    const DWORD n=GetFullPathNameW(p.c_str(),DWORD(full.size()),full.data(),nullptr);
    if(!n||n>=full.size())return false;
    full.resize(n);row.path=std::move(full);
    for(auto& c:row.title)if(c<32||(c>=127&&c<=159))c=L' ';
    if(row.title.empty())row.title=row.path.substr(row.path.find_last_of(L'\\')+1);
    if(row.title.size()>max_title)row.title.resize(max_title);
    // A bounded title must never end halfway through a UTF-16 surrogate pair.
    if(!row.title.empty()&&row.title.back()>=0xd800&&row.title.back()<=0xdbff)row.title.pop_back();
    return true;
}
}
void RecordRecentDownload(const std::wstring& title,const std::wstring& path,std::uint64_t bytes) noexcept {
    try {
        if(path.empty()||path.size()>max_path)return;
        RecentDownload row{title.substr(0,max_title),path,bytes};
        if(!Clean(row))return;
        Exclusive guard;
        // Reserve before mutation so allocation failure retains the old snapshot.
        rows.reserve(kRecentDownloadLimit+1);
        rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const RecentDownload& old){return SamePath(old.path,row.path);}),rows.end());
        rows.insert(rows.begin(),std::move(row));
        if(rows.size()>kRecentDownloadLimit)rows.resize(kRecentDownloadLimit);
    }catch(...) {}
}
bool ReadRecentDownloads(std::vector<RecentDownload>& out) {
    if(!TryAcquireSRWLockShared(&lock))return false;
    try {out=rows;}catch(...) {ReleaseSRWLockShared(&lock);throw;}
    ReleaseSRWLockShared(&lock);return true;
}
}
