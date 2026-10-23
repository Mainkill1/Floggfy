#include "../native/song_destination.cpp"
#include <cassert>
#include <cstdio>
static void WriteAudio(const std::wstring& file) {
    assert(history::EnsureDirectory(file.substr(0,file.find_last_of(L'\\'))));
    HANDLE handle=CreateFileW(history::WindowsPath(file).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);
    assert(handle!=INVALID_HANDLE_VALUE);
    DWORD written=0;assert(WriteFile(handle,"saved",5,&written,nullptr) && written==5);CloseHandle(handle);
}
int main() {
    using namespace history;
    Catalog item;item.artist=L"Album Artist";item.album=L"Album: Deluxe";item.title=L"Song";item.track=3;
    wchar_t temp[MAX_PATH];GetTempPathW(MAX_PATH,temp);
    const auto root=std::wstring(temp)+L"Floggfy-existing-song-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    assert(GetFileAttributesW(root.c_str())==INVALID_FILE_ATTRIBUTES);
    std::wstring saved_file=L"retained";
    assert(!ReadSongDestination(7,11,saved_file) && saved_file==L"retained");
    PublishSongDestination(7,root,item,false,11);
    assert(!ReadSongDestination(7,11,saved_file) && saved_file==L"retained");
    assert(GetFileAttributesW(root.c_str())==INVALID_FILE_ATTRIBUTES); // Probes never create missing folders.
    for(bool music_folder:{false,true}) {
        for(auto kind:{MediaKind::Music,MediaKind::VariousArtists}) {
            item.kind=kind;
            const auto flac=OutputPath(root,item,L".flac",music_folder);
            const auto ogg=OutputPath(root,item,L".ogg",music_folder);
            const auto folder=flac.substr(0,flac.find_last_of(L'\\'));
            assert(EnsureDirectory(folder));
            PublishSongDestination(7,root,item,music_folder,11);
            assert(!ReadSongDestination(7,11,saved_file)); // An empty existing folder is not a saved song.
            assert(CreateDirectoryW(WindowsPath(flac).c_str(),nullptr));
            PublishSongDestination(7,root,item,music_folder,11);
            assert(!ReadSongDestination(7,11,saved_file)); // A directory named *.flac is not audio.
            assert(RemoveDirectoryW(WindowsPath(flac).c_str()));
            WriteAudio(ogg);PublishSongDestination(7,root,item,music_folder,11);
            assert(ReadSongDestination(7,11,saved_file) && saved_file==ogg);
            WriteAudio(flac);PublishSongDestination(7,root,item,music_folder,11);
            assert(ReadSongDestination(7,11,saved_file) && saved_file==flac); // Prefer FLAC when both exist.
            assert(DeleteFileW(WindowsPath(flac).c_str()));
            PublishSongDestination(7,root,item,music_folder,11);
            assert(ReadSongDestination(7,11,saved_file) && saved_file==ogg);
            assert(DeleteFileW(WindowsPath(ogg).c_str()));
            PublishSongDestination(7,root,item,music_folder,11);
            assert(!ReadSongDestination(7,11,saved_file));
        }
    }
    const auto ogg=OutputPath(root,item,L".ogg",true);
    WriteAudio(ogg);PublishSongDestination(7,root,item,true,11);
    assert(ReadSongDestination(7,11,saved_file) && saved_file==ogg);
    const auto before=saved_file;
    AcquireSRWLockExclusive(&destination_lock);
    const auto start=GetTickCount64();
    assert(!ReadSongDestination(7,11,saved_file) && saved_file==before);
    assert(GetTickCount64()-start<100);
    ReleaseSRWLockExclusive(&destination_lock);
    assert(!ReadSongDestination(8,11,saved_file) && saved_file==before);
    assert(!ReadSongDestination(7,12,saved_file) && saved_file==before);
    assert(DeleteFileW(WindowsPath(ogg).c_str()));
    assert(ReadSongDestination(7,11,saved_file) && saved_file==before); // RAM read does no filesystem probes.
    destination_updated=GetTickCount64()-4000;
    assert(!ReadSongDestination(7,11,saved_file) && saved_file==before);
    PublishSongDestination(0,L"",{},false,11);
    assert(!ReadSongDestination(7,11,saved_file) && !ReadSongDestination(0,11,saved_file));
    std::puts("PASS: exact existing saver files only, missing/empty/directory targets disabled, FLAC preference, Ogg fallback, identity/settings/freshness binding and nonblocking RAM reads");
}
