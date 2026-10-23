#include "../native/song_destination.cpp"
#include <cassert>
#include <cstdio>
int main() {
    using namespace history;
    Catalog item;item.artist=L"Album Artist";item.album=L"Album: Deluxe";item.title=L"Song";item.track=3;
    const auto root=L"C:\\Floggfy-nonexistent-destination-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    assert(GetFileAttributesW(root.c_str())==INVALID_FILE_ATTRIBUTES);
    std::wstring folder=L"retained";
    assert(!ReadSongDestination(7,11,folder) && folder==L"retained");
    for(bool music_folder:{false,true}) {
        for(auto kind:{MediaKind::Music,MediaKind::VariousArtists}) {
            item.kind=kind;
            PublishSongDestination(7,root,item,music_folder,11);
            assert(ReadSongDestination(7,11,folder));
            for(const auto* extension:{L".flac",L".ogg"}) {
                const auto saved=OutputPath(root,item,extension,music_folder);
                assert(folder==saved.substr(0,saved.find_last_of(L'\\')));
            }
            assert(GetFileAttributesW(root.c_str())==INVALID_FILE_ATTRIBUTES); // Publishing does no filesystem work.
        }
    }
    const auto before=folder;
    AcquireSRWLockExclusive(&destination_lock);
    const auto start=GetTickCount64();
    assert(!ReadSongDestination(7,11,folder) && folder==before);
    assert(GetTickCount64()-start<100);
    ReleaseSRWLockExclusive(&destination_lock);
    assert(!ReadSongDestination(8,11,folder) && folder==before);
    assert(!ReadSongDestination(7,12,folder) && folder==before); // Same song, changed save settings.
    destination_updated=GetTickCount64()-4000;
    assert(!ReadSongDestination(7,11,folder) && folder==before);
    PublishSongDestination(0,L"",{},false,11);
    assert(!ReadSongDestination(7,11,folder) && !ReadSongDestination(0,11,folder));
    std::puts("PASS: current song destinations share the saver path, require matching fresh identity, never wait and never create directories");
}
