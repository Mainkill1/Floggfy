#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "auto_skip.h"
#include "srw_guard.h"
#include <cctype>

namespace history {
namespace {
SRWLOCK request_lock=SRWLOCK_INIT;
AutoSkipRequest request;

bool MusicUri(const std::string& uri) noexcept {
    constexpr char prefix[]="spotify:track:";
    if(uri.size()!=sizeof(prefix)-1+22 || uri.compare(0,sizeof(prefix)-1,prefix))return false;
    for(size_t i=sizeof(prefix)-1;i<uri.size();++i)
        if(!std::isalnum(static_cast<unsigned char>(uri[i])))return false;
    return true;
}
}

bool ShouldAutoSkipDownloaded(const PlaybackQualitySnapshot& current,
                              std::uint64_t expected_identity,
                              const Quality& existing) noexcept {
    if(!expected_identity || current.identity!=expected_identity ||
       current.format==PlaybackFormat::Unknown || !current.rate || !current.channels ||
       !existing.rate || !existing.channels ||
       (existing.codec==Codec::Flac&&!existing.bits))return false;
    if(current.format==PlaybackFormat::Ogg)return true;
    if(!current.bits)return false;
    const Quality playing{Codec::Flac,current.rate,current.channels,current.bits,0};
    return !HigherQuality(playing,existing);
}

std::uint32_t QueueAutoSkipRequest(const std::string& spotify_uri,unsigned settings_generation) {
    if(!MusicUri(spotify_uri))return 0;
    ExclusiveSrw guard(request_lock);
    auto token=request.token+1;if(!token)++token;
    request={token,settings_generation,spotify_uri};
    return token;
}

AutoSkipRequest ReadAutoSkipRequest() {
    SharedSrw guard(request_lock);
    return request;
}
}
