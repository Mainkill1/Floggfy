#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <utility>
#include <chrono>
namespace history {
bool IsAlbumArtworkUrl(const std::string& url);
// Call only on the metadata worker: bounded synchronous HTTPS fetch, no credentials.
void CacheAlbumArtwork(const std::string& track_uri,const std::string& url);
// Never performs IO or waits for a fetch. On a miss leaves outputs unchanged.
bool GetAlbumArtwork(const std::string& track_uri,std::vector<uint8_t>& bytes,std::string& mime);
class AlbumArtworkCache {
public:
    using Fetch=std::function<bool(const std::string&,std::vector<uint8_t>&)>;
    explicit AlbumArtworkCache(Fetch fetch):fetch_(std::move(fetch)){}
    void Cache(const std::string& uri,const std::string& url);
    bool Get(const std::string& uri,std::vector<uint8_t>& bytes,std::string& mime);
private:
    struct Entry { std::string url,ready_url,mime;std::vector<uint8_t> bytes;std::chrono::steady_clock::time_point attempted;bool pending=false;uint64_t order=0; };
    Fetch fetch_; std::mutex mutex_; std::map<std::string,Entry> entries_;uint64_t order_=0;
};
}
