#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "metadata_update_windows.h"
#include "metadata_update_core.h"
#include "file_publication.h"
#include "../tooling/history_settings.h"
#include "marked_recording.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string_view>
namespace history {
namespace {
struct CloseFile {void operator()(void* file)const{if(file&&file!=INVALID_HANDLE_VALUE)CloseHandle(file);}};
using File=std::unique_ptr<void,CloseFile>;
bool SavedDateField(const std::string& key) {
    constexpr std::string_view date="HISTORY_SAVED_UTC";
    return key.size()==date.size() && std::equal(key.begin(),key.end(),date.begin(),[](char a,char b){
        return (a>='a' && a<='z'?char(a-'a'+'A'):a)==b;
    });
}
std::string CreationStamp(HANDLE file) {
    FILETIME creation{};
    SYSTEMTIME utc{};
    if(!GetFileTime(file,&creation,nullptr,nullptr) || (!creation.dwHighDateTime && !creation.dwLowDateTime) ||
       !FileTimeToSystemTime(&creation,&utc))return {};
    char stamp[32];
    std::snprintf(stamp,sizeof(stamp),"%04u-%02u-%02uT%02u:%02u:%02uZ",
                  unsigned(utc.wYear),unsigned(utc.wMonth),unsigned(utc.wDay),
                  unsigned(utc.wHour),unsigned(utc.wMinute),unsigned(utc.wSecond));
    return ValidMarkedDate(stamp)?stamp:std::string{};
}
bool UpdateEnabled(unsigned expected_generation) {
    if(expected_generation==~0u)return true;
    const auto preferences=GetSettings();
    return preferences.generation==expected_generation && preferences.downloads &&
           preferences.metadata && preferences.update_existing_metadata;
}
}
FileMetadataUpdate UpdateRecordingMetadata(const std::wstring& path,const Tags& incoming,const Catalog& catalog,unsigned expected_generation) {
    if(!UpdateEnabled(expected_generation))return FileMetadataUpdate::Skipped;
    // Retain the exact source object through promotion, preventing a metadata
    // update from replacing another process's newly created recording.
    File file(CreateFileW(WindowsPath(path).c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,
                         nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(file.get()==INVALID_HANDLE_VALUE)return FileMetadataUpdate::Failed;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file.get(),&size)||size.QuadPart<=0||size.QuadPart>500LL*1024*1024)
        return FileMetadataUpdate::Failed;
    std::vector<uint8_t> source(size_t(size.QuadPart));
    DWORD read=0;
    if(!ReadFile(file.get(),source.data(),DWORD(source.size()),&read,nullptr)||read!=source.size())
        return FileMetadataUpdate::Failed;
    std::vector<uint8_t> patched;std::string error;
    Tags candidates=incoming;
    candidates.fields.erase(std::remove_if(candidates.fields.begin(),candidates.fields.end(),
        [](const auto& field){return SavedDateField(field.first);}),candidates.fields.end());
    const auto settings=GetSettings();
    if(settings.generation==expected_generation && settings.marked_when_downloaded) {
        auto saved=CreationStamp(file.get());
        if(!saved.empty())candidates.fields.emplace_back("HISTORY_SAVED_UTC",std::move(saved));
    }
    const auto result=FillRecordingMetadata(source,candidates,catalog,patched,error);
    if(result==MetadataUpdate::Unchanged)return FileMetadataUpdate::Skipped;
    if(result!=MetadataUpdate::Updated)return FileMetadataUpdate::Failed;
    if(!UpdateEnabled(expected_generation))return FileMetadataUpdate::Skipped;
    return PublishMetadataBytes(path,patched.data(),patched.size(),catalog,file.get())==FilePublication::Upgraded
        ? FileMetadataUpdate::Updated : FileMetadataUpdate::Failed;
}
}
