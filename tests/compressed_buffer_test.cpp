#include "../native/compressed_buffer.h"
#include <algorithm>
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
    const auto block=b.BlockAt(65531);
    check(block.size==5 && std::equal(block.data,block.data+block.size,bytes.begin()+65531),"view ends at block boundary");
    check(b.BlockAt(131072).size==bytes.size()-131072 && !b.BlockAt(bytes.size()).data,"final block exposes only initialized bytes");
    std::vector<uint8_t> scratch;
    check(b.ReadContiguous(65531,5,scratch)==block.data && scratch.capacity()==0,"in-block read borrows storage");
    const auto* joined=b.ReadContiguous(65531,80000,scratch);
    check(joined==scratch.data() &&
          scratch==std::vector<uint8_t>(bytes.begin()+65531,bytes.begin()+145531),"unaligned read across three blocks");
    check(!b.ReadContiguous(bytes.size()-1,2,scratch) && !b.ReadContiguous(0,0,scratch),"out-of-bounds and empty ranges");
    CompressedBuffer compact;check(compact.Append(bytes.data(),bytes.size(),3*65536),"restart fixture append");
    check(!compact.RetainPrefixAndSuffix(20,10)&&compact.size()==bytes.size(),"invalid compaction preserves bytes");
    check(compact.RetainPrefixAndSuffix(17,70000)&&compact.size()==bytes.size()-70000+17,"prefix/suffix compaction");
    auto compacted=compact.Flatten();
    check(std::equal(compacted.begin(),compacted.begin()+17,bytes.begin())&&
          std::equal(compacted.begin()+17,compacted.end(),bytes.begin()+70000),
          "compaction retains exact selected bytes");
    CompressedBuffer split,suffix;check(split.Append(bytes.data(),bytes.size(),3*65536),"split fixture");
    check(!split.SplitSuffix(65531,suffix,0)&&split.size()==bytes.size()&&!suffix.size(),"split cap preserves both inputs");
    check(split.SplitSuffix(65531,suffix,2*65536),"split at an unaligned byte boundary");
    check(split.Flatten()==std::vector<uint8_t>(bytes.begin(),bytes.begin()+65531)&&
          suffix.Flatten()==std::vector<uint8_t>(bytes.begin()+65531,bytes.end()),"split preserves exact prefix and suffix across blocks");
    check(b.Flatten()==bytes && b.size()==0 && b.capacity()==0,"completed compilation preserves bytes and releases blocks");
    puts("PASS: bounded fixed RAM blocks, rejection preservation and complete-buffer compilation");
}
