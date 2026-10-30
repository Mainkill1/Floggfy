#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "metadata_update_windows.h"
#include "metadata_update_core.h"
#include "file_publication.h"
#include "history_settings.h"
#include <memory>
namespace history {
namespace {
struct CloseFile {void operator()(void* file)const{if(file&&file!=INVALID_HANDLE_VALUE)CloseHandle(file);}};
using File=std::unique_ptr<void,CloseFile>;
bool UpdateEnabled(unsigned expected_generation) {
    if(expected_generation==~0u)return true;
    const auto preferences=GetSettings();
    return preferences.generation==expected_generation && preferences.downloads &&
           preferences.metadata && preferences.update_existing_metadata;
}
}
FileMetadataUpdate UpdateRecordingMetadata(const std::wstring& path,const Tags& incoming,const Catalog& catalog,unsigned expected_generation,
    bool(*is_current)(void*),void* context) {
    struct Permit {unsigned generation;bool(*current)(void*);void* context;} permit{expected_generation,is_current,context};
    auto enabled=[](void* argument) noexcept {
        try{const auto& p=*static_cast<Permit*>(argument);return UpdateEnabled(p.generation)&&(!p.current||p.current(p.context));}
        catch(...){return false;} // A failed settings/intent check never authorizes replacement.
    };
    if(!enabled(&permit))return FileMetadataUpdate::Skipped;
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
    const auto result=FillRecordingMetadata(source,incoming,catalog,patched,error);
    if(result==MetadataUpdate::Unchanged)return FileMetadataUpdate::Skipped;
    if(result!=MetadataUpdate::Updated)return FileMetadataUpdate::Failed;
    if(!enabled(&permit))return FileMetadataUpdate::Skipped;
    const auto publication=PublishMetadataBytes(path,patched.data(),patched.size(),catalog,file.get(),enabled,&permit);
    return publication==FilePublication::Upgraded?FileMetadataUpdate::Updated:
        publication==FilePublication::Skipped?FileMetadataUpdate::Skipped:FileMetadataUpdate::Failed;
}
}
