#include "../native/rich_metadata.h"
#include <cassert>
#include <iostream>
using namespace history;
int main() {
 RichMetadata m; std::string e;
 assert(ParseRichMetadata("v=1&title=Song&artist=Artist&album=Album&duration=200&uri=spotify%3Atrack%3A123&DATE=2026-09-30&GENRE=Rock&LYRICS=one%0Atwo%20%C3%A9",m,e));
 assert(m.fields.at("LYRICS")=="one\ntwo é" && m.fields.at("DATE")=="2026-09-30");
 assert(m.Matches("Song","Artist","Album",200.5));
 assert(!m.Matches("Song","Other","Album",200));
 assert(!ParseRichMetadata("v=1&title=X&artist=A&album=B&duration=1&uri=x&LYRICS=%GG",m,e));
 assert(!ParseRichMetadata("v=1&title=X&artist=A&album=B&duration=1&uri=x&LYRICS=%FF",m,e));
 assert(!ParseRichMetadata(std::string(131073,'x'),m,e));
 assert(!ParseRichMetadata("v=1&title=X&artist=A&album=B&duration=1&uri=x&UNKNOWN=abc",m,e));
 assert(!ParseRichMetadata("v=1&title=X&artist=A&album=B&duration=1&uri=spotify%3Atrack%3Ax&LYRICS="+std::string(65537,'x'),m,e));
 assert(ParseRichMetadata("v=1&title=Song&artist=Artist&album=Album&duration=200&uri=spotify%3Atrack%3Aa&playback_quality=very_high",m,e));
 assert(m.playback_quality=="very_high" && !m.fields.count("playback_quality"));
 assert(ParseRichMetadata("v=1&title=Song&artist=Artist&album=Album&duration=200&uri=spotify%3Atrack%3Aa&playback_quality=3",m,e));
 assert(m.playback_quality.empty());
 assert(ParsePlaybackMetadata("v=1&title=Episode&uri=spotify%3Aepisode%3A123&duration=4030&playback_quality=high",m,e));
 assert(m.title=="Episode" && m.artist.empty() && m.album.empty() && m.playback_quality=="high");
 const std::string episode_payload="v=1&title=Episode&artist=Show&album=Show&uri=spotify%3Aepisode%3A0000000000000000000005&duration=4030&SPOTIFY_URI=spotify%3Aepisode%3A0000000000000000000005&PUBLISHER=Show";
 assert(ParseRichMetadata(episode_payload,m,e));
 assert(m.uri=="spotify:episode:0000000000000000000005"&&m.artist=="Show"&&m.album=="Show");
 assert(m.fields.at("SPOTIFY_URI")==m.uri&&m.fields.at("PUBLISHER")=="Show");
 MetadataCache episode_cache;episode_cache.Put(m);
 assert(episode_cache.FindUri(m.uri)&&episode_cache.Find("Episode","Show","Show",4030));
 assert(!ParsePlaybackMetadata("v=1&title=Episode&uri=spotify%3Ashow%3A123&duration=4030",m,e));
 assert(ParseRichMetadata("v=1&title=Song&artist=Artist&album=Album&duration=200&uri=spotify%3Atrack%3Aa&artwork_url=https%3A%2F%2Fi.scdn.co%2Fimage%2Fabc",m,e));
 assert(m.artwork_url=="https://i.scdn.co/image/abc" && !m.fields.count("artwork_url"));
 MetadataCache cache;
 assert(ParseRichMetadata("v=1&title=Song&artist=Artist&album=Album&duration=200&uri=spotify%3Atrack%3Aa&DATE=2026",m,e)); cache.Put(m);
 assert(cache.Find("Song","Artist","Album",200));
 m.uri="spotify:track:b"; cache.Put(m); assert(!cache.Find("Song","Artist","Album",200));
 assert(ParseRichMetadata("v=1&title=Song&artist=Artist&album=Album&duration=200&uri=spotify%3Atrack%3A123&ARTIST=Artist%3B%20Guest&ALBUMARTIST=Artist%3B%20Guest&LABEL=Example%20Records&ORGANIZATION=Example%20Records&COPYRIGHT=Example%20Rights&PUBLISHER=Example%20Publishing",m,e));
 assert(m.artist=="Artist" && m.fields.at("ARTIST")=="Artist; Guest");
 assert(m.fields.at("LABEL")=="Example Records" && m.fields.at("PUBLISHER")=="Example Publishing");
 const std::string album_uri="spotify:album:0000000000000000000002";
 assert(ParseRichMetadata("v=1&title=Song&artist=Artist&album=Album&duration=200&uri=spotify%3Atrack%3A0000000000000000000001&album_uri=spotify%3Aalbum%3A0000000000000000000002&SPOTIFY_ALBUM_URI=spotify%3Aalbum%3A0000000000000000000002",m,e));
 assert(m.fields.at("SPOTIFY_ALBUM_URI")==album_uri);
 assert(!ParseRichMetadata("v=1&title=Song&artist=Artist&album=Album&duration=200&uri=spotify%3Atrack%3A0000000000000000000001&album_uri=spotify%3Aalbum%3Ashort",m,e));
 std::cout<<"PASS: bounded UTF-8 metadata, identity matching and ambiguity rejection\n";
}
