#include "../native/hook_startup_batch.h"
#include "../native/hook_init_state.h"
#include <cassert>
#include <vector>
#include <cstdio>

struct FakeHooks {
    bool live[5]{},queued[5]{};
    std::vector<int> calls;
    int queue_failure=0,clear_failure=0,disable_failure=0,remove_failure=0;
    bool apply_failure=false,quiescent=true;
    bool Queue(void* p) {int id=int(reinterpret_cast<std::uintptr_t>(p));calls.push_back(10+id);if(queue_failure==id)return false;queued[id]=true;return true;}
    bool Clear(void* p) {int id=int(reinterpret_cast<std::uintptr_t>(p));calls.push_back(20+id);if(clear_failure==id)return false;queued[id]=false;return true;}
    bool Disable(void* p) {int id=int(reinterpret_cast<std::uintptr_t>(p));calls.push_back(30+id);if(disable_failure==id)return false;live[id]=false;return true;}
    bool Remove(void* p) {int id=int(reinterpret_cast<std::uintptr_t>(p));calls.push_back(40+id);return remove_failure!=id;}
    bool Wait() {calls.push_back(50);return quiescent;}
    bool Apply() {calls.push_back(60);for(int i=1;i<5;i++){live[i]=queued[i];if(apply_failure&&i==2)return false;}return true;}
};
static void* Target(int id) {return reinterpret_cast<void*>(std::uintptr_t(id));}
template<std::size_t N> static bool Rollback(hooks::QueuedHookSet<N>& set,FakeHooks& fake,bool remove=true) {
    return set.Rollback([&](void* p){return fake.Clear(p);},[&](void* p){return fake.Disable(p);},[&]{return fake.Wait();},[&](void* p){return remove&&fake.Remove(p);}).CanRelease();
}
int main() {
    // The same coordinator and owned-target rollback are used by native startup.
    for(bool fail_apply:{false,true}) {
        FakeHooks fake;fake.apply_failure=fail_apply;
        hooks::QueuedHookSet<3> metadata;hooks::QueuedHookSet<1> menu;
        hooks::InitController metadata_init,menu_init;
        assert(metadata_init.TryBegin(0)&&menu_init.TryBegin(0));
        auto prepare_metadata=[&]{fake.calls.push_back(1);for(int i=1;i<=3;i++)assert(metadata.Add(Target(i)));return metadata.Queue([&](void* p){return fake.Queue(p);});};
        auto prepare_menu=[&]{fake.calls.push_back(2);assert(menu.Add(Target(4)));return menu.Queue([&](void* p){return fake.Queue(p);});};
        auto finish_metadata=[&](bool ok){assert(fake.calls[6]==60);if(ok)metadata_init.Activate();else{assert(!Rollback(metadata,fake,false));metadata_init.MarkUnsupported();}};
        auto finish_menu=[&](bool ok){if(ok)menu_init.Activate();else{assert(!Rollback(menu,fake,false));menu_init.MarkUnsupported();}};
        hooks::StartTogether(prepare_metadata,prepare_menu,[&]{assert(metadata_init.State()==hooks::InitState::Installing&&menu_init.State()==hooks::InitState::Installing);return fake.Apply();},finish_metadata,finish_menu);
        // Exactly one apply; both sets queued first, before any thread resumes.
        const std::vector<int> prefix={1,11,12,13,2,14,60};
        assert(std::vector<int>(fake.calls.begin(),fake.calls.begin()+7)==prefix);
        assert(metadata_init.State()==(fail_apply?hooks::InitState::Unsupported:hooks::InitState::Active));
        assert(menu_init.State()==(fail_apply?hooks::InitState::Unsupported:hooks::InitState::Active));
        if(fail_apply){for(int i=1;i<5;i++)assert(!fake.live[i]&&!fake.queued[i]);assert(metadata.Size()==3&&menu.Size()==1);for(int call:fake.calls)assert(call<41||call>44);assert(!metadata_init.TryBegin(1000));}
    }
    // Queue failure must retain ownership of every created trampoline for cleanup.
    FakeHooks failed_queue;failed_queue.queue_failure=2;hooks::QueuedHookSet<3> set;
    for(int i=1;i<=3;i++)assert(set.Add(Target(i)));
    assert(!set.Queue([&](void* p){return failed_queue.Queue(p);}));
    hooks::InitController setup;assert(setup.TryBegin(0));
    if(Rollback(set,failed_queue))setup.Retry(0);else setup.MarkUnsupported();
    assert(setup.State()==hooks::InitState::RetryableFailure&&set.Size()==0);
    for(int call:failed_queue.calls)assert(call!=60);
    for(int i=1;i<=3;i++)assert(!failed_queue.queued[i]);
    // Before application, cleanup still refuses release when a cleanup operation fails.
    for(int failure=1;failure<=4;failure++){
        FakeHooks fake;hooks::QueuedHookSet<1> live;assert(live.Add(Target(1)));
        assert(live.Queue([&](void* p){return fake.Queue(p);}));
        fake.clear_failure=failure==4?1:0;fake.disable_failure=failure==1?1:0;fake.quiescent=failure!=2;fake.remove_failure=failure==3?1:0;
        assert(!Rollback(live,fake));assert(live.Size()==1);
        if(failure<=2||failure==4)for(int call:fake.calls)assert(call!=41);
    }
    // Disabled/already-active participants do not trigger apply or false finalization.
    int applies=0,finalizes=0;
    hooks::StartTogether([]{return false;},[]{return false;},[&]{++applies;return true;},[&](bool){++finalizes;},[&](bool){++finalizes;});
    assert(applies==0&&finalizes==0);
    hooks::StartTogether([]{return false;},[]{return true;},[&]{++applies;return false;},[&](bool){assert(false);},[&](bool ok){assert(!ok);++finalizes;});
    assert(applies==1&&finalizes==1);
    std::puts("PASS: startup queue ordering, single activation, partial apply rollback, owned queues and retry states");
}
