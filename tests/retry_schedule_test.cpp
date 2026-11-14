#include "../native/retry_schedule.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <limits>

static bool deny_allocation=false;
void* operator new(std::size_t size) {
    if(deny_allocation)throw std::bad_alloc();
    if(void* p=std::malloc(size?size:1))return p;
    throw std::bad_alloc();
}
__attribute__((noinline)) void operator delete(void* p) noexcept {std::free(p);}
__attribute__((noinline)) void operator delete(void* p,std::size_t) noexcept {std::free(p);}

int main() {
    using Job=std::unique_ptr<int>;
    history::RetrySchedule<Job> retries;
    assert(!retries.NextDeadline()&&!retries.PopDue(0));
    retries.Push(std::make_unique<int>(1),30000);
    retries.Push(std::make_unique<int>(2),2000);
    assert(retries.NextDeadline()==2000);
    assert(!retries.PopDue(1999));
    auto earlier=retries.PopDue(2000);
    assert(earlier&&**earlier==2);
    assert(retries.NextDeadline()==30000);
    retries.Push(std::make_unique<int>(3),30000);
    retries.Push(std::make_unique<int>(4),30000);
    for(int expected:{1,3,4}) {
        auto due=retries.PopDue(30000);
        assert(due&&**due==expected);
    }
    assert(!retries.NextDeadline());
    // UINT64_MAX remains a real deadline, not an empty sentinel or signed time.
    retries.Push(std::make_unique<int>(5),std::numeric_limits<uint64_t>::max());
    assert(!retries.PopDue(std::numeric_limits<uint64_t>::max()-1));
    assert(**retries.PopDue(std::numeric_limits<uint64_t>::max())==5);
    retries.Push(std::make_unique<int>(7),20);
    auto incoming=std::make_unique<int>(6);
    deny_allocation=true;
    try {retries.Push(std::move(incoming),10);assert(false);}catch(const std::bad_alloc&){}
    deny_allocation=false;
    assert(incoming&&*incoming==6&&retries.NextDeadline()==20);
    assert(**retries.PopDue(20)==7&&!retries.NextDeadline());
    // Shutdown drains move-only ownership through the normal destructor.
    struct Counted {int* alive;explicit Counted(int& count):alive(&count){++*alive;}~Counted(){--*alive;}};
    int alive=0;
    {history::RetrySchedule<std::unique_ptr<Counted>> shutdown;
     shutdown.Push(std::make_unique<Counted>(alive),100);
     shutdown.Push(std::make_unique<Counted>(alive),200);
     assert(alive==2);}
    assert(alive==0);
    std::puts("PASS: retry deadlines, stable ties, move-only ownership and shutdown");
}
