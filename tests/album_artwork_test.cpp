#include "../native/album_artwork.h"
#include "../native/image_info.h"
#include "artwork_fixture.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <atomic>
static void check(bool b,const char* s){if(!b){std::fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
int main(){
    auto png=artwork_fixture::png();history::ImageInfo info;
    check(history::ReadImageInfo(png,info)&&info.width==1&&info.height==1&&info.depth==32&&info.mime=="image/png","PNG image metadata");
    auto bad=png;bad.resize(24);check(!history::ReadImageInfo(bad,info),"truncated PNG rejected");
    bad=png;bad[16]=0xff;check(!history::ReadImageInfo(bad,info),"oversize or corrupt image rejected");
    bad=png;bad[45]^=1;check(!history::ReadImageInfo(bad,info),"corrupt PNG chunk rejected");
    check(!history::ReadImageInfo({0xff,0xd8,0xff,0xd9},info),"empty JPEG rejected");
    auto jpeg=artwork_fixture::jpeg();
    check(history::ReadImageInfo(jpeg,info)&&info.width==3&&info.height==2&&info.depth==24&&info.mime=="image/jpeg","JPEG dimensions and depth");
    jpeg.pop_back();check(!history::ReadImageInfo(jpeg,info),"truncated JPEG rejected");
    bad={137,80,78,71,13,10,26,10};
    artwork_fixture::chunk(bad,"IHDR",{0,0,32,1,0,0,0,1,8,6,0,0,0});
    artwork_fixture::chunk(bad,"IDAT",{1});artwork_fixture::chunk(bad,"IEND",{});
    check(!history::ReadImageInfo(bad,info),"valid CRC cannot conceal oversized image dimensions");
    const std::string url="https://i.scdn.co/image/ab67616d0000b2730123456789abcdef01234567";
    check(history::IsAlbumArtworkUrl(url),"Spotify CDN accepted");
    for(const auto& u:{"http://i.scdn.co/image/abc","https://i.scdn.co.example.invalid/image/abc","https://@i.scdn.co/image/abc","https://i.scdn.co/image/abc?variant=example","https://example.invalid/image/abc"})check(!history::IsAlbumArtworkUrl(u),"non-public or unexpected CDN URL rejected");
    int calls=0;bool fail=false;history::AlbumArtworkCache cache([&](const std::string&,std::vector<uint8_t>& out){++calls;out=fail?std::vector<uint8_t>{1,2,3}:png;return true;});
    std::vector<uint8_t> bytes;std::string mime;
    cache.Cache("spotify:track:a",url);check(cache.Get("spotify:track:a",bytes,mime)&&bytes==png&&mime=="image/png","validated art cached by track");
    cache.Cache("spotify:track:a",url);check(calls==1,"repeated metadata avoids repeated network");
    fail=true;cache.Cache("spotify:track:b",url);check(!cache.Get("spotify:track:b",bytes,mime),"invalid response never cached");
    cache.Cache("spotify:track:b",url);check(calls==2,"failed fetch rate limited");
    cache.Cache("spotify:track:a","https://i.scdn.co/image/ab67616d0000b2730123456789abcdef01234568");
    check(cache.Get("spotify:track:a",bytes,mime)&&bytes==png,"failed URL refresh preserves valid art");
    fail=false;
    for(int k=0;k<12;++k)cache.Cache("spotify:track:"+std::to_string(k),url);
    check(!cache.Get("spotify:track:0",bytes,mime)&&cache.Get("spotify:track:11",bytes,mime),"bounded cache evicts oldest track");
    std::atomic<bool> entered{false},release{false};
    history::AlbumArtworkCache slow([&](const std::string&,std::vector<uint8_t>& out){entered=true;while(!release)std::this_thread::yield();out=png;return true;});
    std::thread worker([&]{slow.Cache("spotify:track:a",url);});
    while(!entered)std::this_thread::yield();
    check(!slow.Get("spotify:track:a",bytes,mime),"cached reads do not wait for in-flight fetch");
    release=true;worker.join();check(slow.Get("spotify:track:a",bytes,mime),"worker publishes completed image");
    std::puts("PASS: Spotify artwork validation and bounded cache");
}
