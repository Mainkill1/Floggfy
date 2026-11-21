#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "auto_skip.h"
#include "../tooling/srw_guard.h"
#include "spotify_uri.h"

namespace history {
namespace {
SRWLOCK request_lock=SRWLOCK_INIT;
AutoSkipRequest request;

bool PlaybackInstance(const std::string& instance) noexcept {
    if(instance.empty()||instance.size()>128)return false;
    for(const unsigned char c:instance)if(c<0x20||c>0x7e)return false;
    return true;
}
}

bool ShouldAutoSkipDownloaded(const PlaybackQualitySnapshot& current,
                              std::uint64_t expected_identity,
                              std::string_view expected_playback_instance,
                              const Quality& existing) noexcept {
    if(current.display_only || !expected_identity || current.identity!=expected_identity ||
       !current.MatchesPlaybackInstance(expected_playback_instance) ||
       current.format==PlaybackFormat::Unknown || !current.rate || !current.channels ||
       !existing.rate || !existing.channels ||
       (existing.codec==Codec::Flac&&!existing.bits))return false;
    if(current.format==PlaybackFormat::Ogg)return true;
    if(!current.bits)return false;
    const Quality playing{Codec::Flac,current.rate,current.channels,current.bits,0};
    return !HigherQuality(playing,existing);
}

std::uint32_t QueueAutoSkipRequest(const std::string& spotify_uri,
                                   const std::string& playback_instance,
                                   unsigned settings_generation) {
    if(!ValidSpotifyTrackUri(spotify_uri)||!PlaybackInstance(playback_instance))return 0;
    ExclusiveSrw guard(request_lock);
    auto token=request.token+1;if(!token)++token;
    request={token,settings_generation,spotify_uri,playback_instance};
    return token;
}

AutoSkipRequest ReadAutoSkipRequest() {
    SharedSrw guard(request_lock);
    return request;
}
}
