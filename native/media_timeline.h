#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace history::media_detail {
// WinRT TimeSpan/DateTime values use 100 ns ticks. Position is sampled at
// LastUpdatedTime, not at the time the observer retrieves the properties.
struct MediaTimeline {
    int64_t start=0,end=0,position=0,updated=0;
    int status=0;

    bool Valid() const {
        return end>start && position>=start && position<=end && updated>0 &&
            static_cast<long double>(end)-start<=86400.0L*10000000;
    }
    bool ContinuesTo(const MediaTimeline& next) const {
        if(!Valid() || !next.Valid() || start!=next.start || end!=next.end ||
           status!=next.status || next.updated<updated || next.position<position)
            return false;
        // A paused/stopped timeline may refresh its timestamp, but not move.
        if(status!=4) return position==next.position;

        // Same session/bounds/status are necessary but not sufficient: a seek
        // or wrap must not attach an old property result to a different play.
        // Compare samples at their own UTC timestamps at normal 1x playback.
        // No integer subtraction can overflow; clamp at the natural track end.
        const auto elapsed=static_cast<long double>(next.updated)-updated;
        const auto expected=std::min(static_cast<long double>(end),position+elapsed);
        constexpr long double rounding_ticks=0.25L*10000000;
        return std::fabs(static_cast<long double>(next.position)-expected)<=rounding_ticks;
    }
};
}
