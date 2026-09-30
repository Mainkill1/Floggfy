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
 MetadataCache cache;
 assert(ParseRichMetadata("v=1&title=Song&artist=Artist&album=Album&duration=200&uri=spotify%3Atrack%3Aa&DATE=2026",m,e)); cache.Put(m);
 assert(cache.Find("Song","Artist","Album",200));
 m.uri="spotify:track:b"; cache.Put(m); assert(!cache.Find("Song","Artist","Album",200));
 std::cout<<"PASS: bounded UTF-8 metadata, identity matching and ambiguity rejection\n";
}
