#pragma once
#include "hook_rollback.h"
#include <array>
#include <cstddef>

namespace hooks {
// Created trampolines remain owned until rollback proves them safe to remove.
// Queueing changes no executable code and cannot make an initializer Active.
template<std::size_t Capacity> class QueuedHookSet {
    std::array<void*,Capacity> targets_{};
    std::size_t count_=0;
public:
    bool Add(void* target) noexcept {
        if(!target||count_==Capacity)return false;
        for(std::size_t i=0;i<count_;++i)if(targets_[i]==target)return false;
        targets_[count_++]=target;return true;
    }
    std::size_t Size() const noexcept {return count_;}
    template<class QueueFn> bool Queue(QueueFn queue) {
        bool ok=count_!=0;
        for(std::size_t i=0;i<count_;++i)ok=queue(targets_[i])&&ok;
        return ok;
    }
    template<class Clear,class Disable,class Wait,class Remove>
    RollbackStatus Rollback(Clear clear,Disable disable,Wait wait,Remove remove) {
        RollbackStatus result;
        // DisableHook(ERROR_DISABLED) does not clear MinHook's queued enable.
        for(std::size_t i=0;i<count_;++i)result.ObserveDisable(clear(targets_[i]));
        for(std::size_t i=0;i<count_;++i)result.ObserveDisable(disable(targets_[i]));
        result.ObserveQuiescence(wait());
        if(result.disabled&&result.quiescent)
            for(std::size_t i=0;i<count_;++i)result.ObserveRemove(remove(targets_[i]));
        if(result.CanRelease()){targets_.fill(nullptr);count_=0;}
        return result;
    }
};

// Both initial root hook sets must be prepared before the only suspension.
// Each owner retains responsibility for worker/dependent-hook lifetime on failure.
template<class PrepareFirst,class PrepareSecond,class Apply,class FinishFirst,class FinishSecond>
void StartTogether(PrepareFirst first,PrepareSecond second,Apply apply,FinishFirst finish_first,FinishSecond finish_second) {
    const bool prepared_first=first(),prepared_second=second();
    if(!prepared_first&&!prepared_second)return;
    const bool applied=apply();
    if(prepared_first)finish_first(applied);
    if(prepared_second)finish_second(applied);
}
} // namespace hooks
