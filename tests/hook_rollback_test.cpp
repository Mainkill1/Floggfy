#include "../native/hook_rollback.h"

#include <cassert>

int main() {
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
