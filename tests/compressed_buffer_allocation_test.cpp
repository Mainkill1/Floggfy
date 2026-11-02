#include "../native/compressed_buffer.h"
#include <cassert>
#include <cstdlib>
#include <new>
#include <cstdio>
static thread_local long allowance=-1;
static long live_allocations=0;
void* operator new(std::size_t size) {
    if(allowance==0)throw std::bad_alloc();
    if(allowance>0)--allowance;
    if(void* p=std::malloc(size?size:1)){++live_allocations;return p;}
    throw std::bad_alloc();
}
void* operator new[](std::size_t size){return ::operator new(size);}
__attribute__((noinline)) void operator delete(void* p) noexcept {if(p)--live_allocations;std::free(p);}
__attribute__((noinline)) void operator delete[](void* p) noexcept {::operator delete(p);}
__attribute__((noinline)) void operator delete(void* p,std::size_t) noexcept {::operator delete(p);}
__attribute__((noinline)) void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
int main() {
    std::vector<uint8_t> bytes(200000,42);
    for(long fault=0;fault<12;++fault) {
        const auto baseline=live_allocations;
        {
        history::CompressedBuffer buffer;bool failed=false;
        allowance=fault;
        try {buffer.Append(bytes.data(),bytes.size(),1024*1024);}catch(const std::bad_alloc&){failed=true;}
        allowance=-1;
        if(failed) {
            assert(buffer.size()==0);
            // A partially allocated buffer must remain reusable with its spare
            // blocks, including a smaller append requiring no new allocation.
            const auto existing=buffer.capacity();
            assert(buffer.Append(bytes.data(),16,existing?0:65536));
            assert(buffer.Flatten()==std::vector<uint8_t>(16,42));
        }else {
            allowance=0;try{buffer.Flatten();assert(false);}catch(const std::bad_alloc&){}
            allowance=-1;assert(buffer.size()==bytes.size());assert(buffer.Flatten()==bytes);
        }
        }
        assert(live_allocations==baseline);
    }
    std::puts("PASS: allocation failure at each buffer growth/flatten step preserves bytes and permits recovery");
}
