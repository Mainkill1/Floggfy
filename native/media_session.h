#pragma once
#include <windows.h>
#include "image_info.h"
#include <string>
#include <vector>
namespace history {
struct Media {
    std::wstring title, artist, album, album_artist, app, genre;
    unsigned track = 0;
    double position = 0, duration = 0;
    bool playing = false;
    std::vector<unsigned char> cover;
    std::wstring cover_extension;
    std::string Key() const;
};
std::string Utf8(const std::wstring& s);
std::string Json(const std::string& s);
class MediaReader {
    void* manager_ = nullptr;
    bool initialized_ = false;
    bool ReadSnapshot(Media& out,bool include_artwork,const Media* expected);
public:
    ~MediaReader();
    bool Read(Media& out,bool include_artwork=false);
    // Dedicated thumbnail worker only; validates the same SMTC track both
    // before and after the asynchronous thumbnail operation.
    bool ReadArtwork(const Media& expected,Media& out);
};
// Capture callers only queue a bounded identity; publication only reads RAM.
// Both functions are independent of Metadata and never wait for thumbnail I/O.
void RequestArtwork(const Media& media) noexcept;
bool GetArtwork(const Media& media,std::vector<uint8_t>& bytes,std::wstring& extension) noexcept;
}
