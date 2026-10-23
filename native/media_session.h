#pragma once
#include <windows.h>
#include <string>
namespace history {
struct Media {
    std::wstring title, artist, album, album_artist, app, genre;
    unsigned track = 0;
    double position = 0, duration = 0;
    bool playing = false;
    std::string Key() const;
};
std::string Utf8(const std::wstring& s);
std::string Json(const std::string& s);
class MediaReader {
    void* manager_ = nullptr;
    bool initialized_ = false;
public:
    ~MediaReader();
    // Basic track properties and timeline only; artwork belongs to enrichment.
    bool Read(Media& out);
};
}
