#pragma once
#include <windows.h>
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
    Media cached_;
public:
    ~MediaReader();
    bool Read(Media& out);
};
}
