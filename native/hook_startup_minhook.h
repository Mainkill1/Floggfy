#pragma once
#include "hook_startup_batch.h"
#include "vendor/minhook/include/MinHook.h"
namespace hooks {
template<std::size_t N> bool QueueStartupHooks(QueuedHookSet<N>& targets) {
    return targets.Queue([](void* p){return MH_QueueEnableHook(p)==MH_OK;});
}
template<std::size_t N,class Wait> RollbackStatus RollbackStartupHooks(QueuedHookSet<N>& targets,Wait wait,bool remove_trampolines=true) {
    return targets.Rollback(
        [](void* p){const auto s=MH_QueueDisableHook(p);return s==MH_OK||s==MH_ERROR_NOT_CREATED;},
        [](void* p){const auto s=MH_DisableHook(p);return s==MH_OK||s==MH_ERROR_DISABLED||s==MH_ERROR_NOT_CREATED;},
        wait,
        [remove_trampolines](void* p){if(!remove_trampolines)return false;const auto s=MH_RemoveHook(p);return s==MH_OK||s==MH_ERROR_NOT_CREATED;});
}
} // namespace hooks
