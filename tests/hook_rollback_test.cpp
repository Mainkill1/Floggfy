#include "../native/hook_rollback.h"

#include <cassert>

int main() {
    for(unsigned failure=0;failure<4;++failure) {
        unsigned creates=0,queues=0,removes=0;
        const auto result=hooks::PrepareOptional([&]{++creates;return failure!=1;},[&]{++queues;return failure==0;},[&]{++removes;return failure!=3;});
        assert(creates==1&&queues==(failure==1?0u:1u)&&removes==(failure>=2?1u:0u));
        assert(result==(failure==0?hooks::OptionalPreparation::Ready:failure==3?hooks::OptionalPreparation::UnsafeRollback:hooks::OptionalPreparation::Unavailable));
    }
    hooks::RollbackStatus clean;
    clean.ObserveDisable(true);
    clean.ObserveRemove(true);
    clean.ObserveQuiescence(true);
    assert(clean.CanRelease());

    hooks::RollbackStatus live_hook;
    live_hook.ObserveDisable(false);
    live_hook.ObserveRemove(true);
    live_hook.ObserveQuiescence(true);
    assert(!live_hook.CanRelease());

    hooks::RollbackStatus stuck_trampoline;
    stuck_trampoline.ObserveDisable(true);
    stuck_trampoline.ObserveRemove(false);
    stuck_trampoline.ObserveQuiescence(true);
    assert(!stuck_trampoline.CanRelease());

    hooks::RollbackStatus active_callback;
    active_callback.ObserveDisable(true);
    active_callback.ObserveRemove(true);
    active_callback.ObserveQuiescence(false);
    assert(!active_callback.CanRelease());

    hooks::CallbackCounter counter;
    assert(counter.Active() == 0);
    {
        auto guard = counter.Enter();
        assert(counter.Active() == 1);
    }
    assert(counter.Active() == 0);
}
