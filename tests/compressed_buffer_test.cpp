#include "../native/compressed_buffer.h"
#include <cstdio>
#include <cstdlib>
using namespace history;
static void check(bool value,const char* name) { if(!value) { fprintf(stderr,"FAIL: %s\n",name); exit(1); } }
int main() {
    CompressedBuffer b; std::vector<uint8_t> bytes(150001);
    for(size_t i=0;i<bytes.size();i++) bytes[i]=uint8_t(i*31);
    check(b.Append(bytes.data(),65530,65536) && b.capacity()==65536,"one bounded block");
    check(b.Append(bytes.data()+65530,bytes.size()-65530,2*65536) && b.capacity()==3*65536,"cross-block append allocates only new blocks");
    check(!b.Append(bytes.data(),65536,0) && b.size()==bytes.size() && b.capacity()==3*65536,"memory cap rejection preserves bytes and allocations");
    for(size_t i=0;i<bytes.size();i++) check(b[i]==bytes[i],"random-access metadata bytes");
    check(b.Flatten()==bytes && b.size()==0 && b.capacity()==0,"completed compilation preserves bytes and releases blocks");
    puts("PASS: bounded fixed RAM blocks, rejection preservation and complete-buffer compilation");
}
