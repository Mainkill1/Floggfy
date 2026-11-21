#include "../native/ui/marked_protocol.h"
#include <cassert>
int main(){
 std::string request;std::vector<std::string> uris;
 const std::wstring prefix=L"FLOGGFY_MARKED_V1:r_1|",uri=L"spotify:track:5PbMSJZcNA3p2LZv7C56cm";
 assert(history::ParseMarkedQuery(prefix+uri,request,uris)&&request=="r_1"&&uris.size()==1);
 assert(!history::ParseMarkedQuery(prefix+uri+L"|",request,uris));
 assert(!history::ParseMarkedQuery(L"FLOGGFY_MARKED_V1:r\"|"+uri,request,uris));
 assert(!history::ParseMarkedQuery(prefix+L"C:\\Music\\file.flac",request,uris));
 assert(!history::ParseMarkedQuery(prefix+uri+L"\u0100",request,uris));
 std::wstring batch=prefix+uri;for(int i=1;i<256;i++)batch+=L"|"+uri;
 assert(history::ParseMarkedQuery(batch,request,uris)&&uris.size()==256);
 assert(!history::ParseMarkedQuery(batch+L"|"+uri,request,uris));
}
