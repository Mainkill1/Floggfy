#define WIN32_LEAN_AND_MEAN
#include "song_destination.h"
#include <windows.h>
namespace history {
namespace {
SRWLOCK destination_lock=SRWLOCK_INIT;
std::uint64_t destination_identity=0;
unsigned destination_generation=0;
ULONGLONG destination_updated=0;
std::wstring destination_folder;
}
void PublishSongDestination(std::uint64_t identity,const std::wstring& root,
                            const Catalog& catalog,bool music_folder,unsigned settings_generation) {
    std::wstring folder;
    if(identity && !root.empty()) {
        // Codec and filename do not change the containing directory.
        const auto file=OutputPath(root,catalog,L".flac",music_folder);
        folder=file.substr(0,file.find_last_of(L"\\/"));
    }
    AcquireSRWLockExclusive(&destination_lock);
    destination_folder.swap(folder);
    destination_identity=identity;
    destination_generation=settings_generation;
    destination_updated=GetTickCount64();
    ReleaseSRWLockExclusive(&destination_lock);
}
bool ReadSongDestination(std::uint64_t identity,unsigned settings_generation,std::wstring& folder) {
    if(!identity || !TryAcquireSRWLockShared(&destination_lock)) return false;
    struct Unlock {~Unlock(){ReleaseSRWLockShared(&destination_lock);}} unlock;
    if(identity!=destination_identity || settings_generation!=destination_generation || destination_folder.empty() ||
       GetTickCount64()-destination_updated>3000) return false;
    folder=destination_folder;
    return true;
}
}
