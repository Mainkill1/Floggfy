#define WIN32_LEAN_AND_MEAN
#include "song_destination.h"
#include "history_settings.h"
#include <windows.h>
#include <utility>
namespace history {
namespace {
SRWLOCK destination_lock=SRWLOCK_INIT;
std::uint64_t destination_identity=0;
unsigned destination_generation=0;
ULONGLONG destination_updated=0;
std::wstring destination_file;
}
void PublishSongDestination(std::uint64_t identity,const std::wstring& root,
                            const Catalog& catalog,bool music_folder,unsigned settings_generation) {
    std::wstring saved_file;
    if(identity && !root.empty()) {
        // Probe only exact saver paths on the audio worker. Prefer lossless
        // when both publications exist; never create a planned destination.
        for(const auto* extension:{L".flac",L".ogg"}) {
            auto file=OutputPath(root,catalog,extension,music_folder);
            const auto attributes=GetFileAttributesW(WindowsPath(file).c_str());
            if(attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY)) {
                saved_file=std::move(file);break;
            }
        }
    }
    AcquireSRWLockExclusive(&destination_lock);
    destination_file.swap(saved_file);
    destination_identity=identity;
    destination_generation=settings_generation;
    destination_updated=GetTickCount64();
    ReleaseSRWLockExclusive(&destination_lock);
}
bool ReadSongDestination(std::uint64_t identity,unsigned settings_generation,std::wstring& saved_file) {
    if(!identity || !TryAcquireSRWLockShared(&destination_lock)) return false;
    struct Unlock {~Unlock(){ReleaseSRWLockShared(&destination_lock);}} unlock;
    if(identity!=destination_identity || settings_generation!=destination_generation || destination_file.empty() ||
       GetTickCount64()-destination_updated>3000) return false;
    saved_file=destination_file;
    return true;
}
}
