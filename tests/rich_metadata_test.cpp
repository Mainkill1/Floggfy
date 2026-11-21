#include "../native/media/rich_metadata.h"
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
 assert(ParseRichMetadata("v=1&title=Episode&uri=spotify%3Aepisode%3A123&duration=4030&playback_quality=high",m,e));
 assert(m.title=="Episode" && m.artist.empty() && m.album.empty() && m.playback_quality=="high");
 const std::string episode_payload="v=1&title=Episode&artist=Show&album=Show&uri=spotify%3Aepisode%3A0000000000000000000005&duration=4030&SPOTIFY_URI=spotify%3Aepisode%3A0000000000000000000005&PUBLISHER=Show";
 assert(ParseRichMetadata(episode_payload,m,e));
 assert(m.uri=="spotify:episode:0000000000000000000005"&&m.artist=="Show"&&m.album=="Show");
 assert(m.fields.at("SPOTIFY_URI")==m.uri&&m.fields.at("PUBLISHER")=="Show");
 assert(ParseRichMetadata("v=1&title=Episode&uri=spotify%3Aepisode%3A123&duration=4030&DATE=2025-09-10",m,e));
 assert(m.artist.empty()&&m.album.empty()&&m.fields.at("DATE")=="2025-09-10");
 assert(ParseRichMetadata(episode_payload,m,e));
 MetadataCache episode_cache;episode_cache.Put(m);
 assert(episode_cache.FindUri(m.uri)&&episode_cache.Find("Episode","Show","Show",4030));
 assert(!ParseRichMetadata("v=1&title=Episode&uri=spotify%3Ashow%3A123&duration=4030",m,e));
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
 // Matcher/save ownership survives registry pressure and receives later updates.
 RichMetadata owned=m;owned.uri="spotify:track:owned";
 auto pin=cache.Pin(owned);const auto original=pin.get();
 for(int i=0;i<80;++i){auto other=m;other.uri="spotify:track:later"+std::to_string(i);cache.Put(other);}
 assert(cache.FindUri(owned.uri)&&cache.Pin(owned).get()==original);
 owned.fields["DATE"]="2025-09-10";cache.Put(owned);
 assert(pin->value.fields.at("DATE")=="2025-09-10");
 std::weak_ptr<MetadataRecord> weak=pin;pin.reset();
 for(int i=0;i<30;++i){auto other=m;other.uri="spotify:track:cleanup"+std::to_string(i);cache.Put(other);}
 assert(weak.expired());
 const std::string image="https://i.scdn.co/image/"+std::string(40,'a');
 assert(ParseRichMetadata("v=1&title=Episode&uri=spotify%3Aepisode%3Ax&duration=20&artwork_urls="+image+"%0Ahttps%3A%2F%2Fexample.com%2Fimage",m,e));
 assert(m.artwork_urls.size()==1&&m.artwork_urls.front()==image);
 std::cout<<"PASS: bounded UTF-8 metadata, identity matching and ambiguity rejection\n";
}
