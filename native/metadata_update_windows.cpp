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
}
FileMetadataUpdate UpdateRecordingMetadata(const std::wstring& path,const Tags& incoming,const Catalog& catalog) {
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
    return PublishMetadataBytes(path,patched.data(),patched.size(),catalog,file.get())==FilePublication::Upgraded
        ? FileMetadataUpdate::Updated : FileMetadataUpdate::Failed;
}
}
