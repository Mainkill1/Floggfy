#pragma once

#include <atomic>

namespace hooks {

struct RollbackStatus {
    bool disabled = true;
    bool removed = true;
    bool quiescent = true;

    void ObserveDisable(bool safe) noexcept { disabled = disabled && safe; }
    void ObserveRemove(bool safe) noexcept { removed = removed && safe; }
    void ObserveQuiescence(bool safe) noexcept { quiescent = quiescent && safe; }
    bool CanRelease() const noexcept { return disabled && removed && quiescent; }
};

class CallbackCounter {
public:
    class Guard {
    public:
        explicit Guard(CallbackCounter& owner) noexcept : owner_(owner) {
            owner_.active_.fetch_add(1, std::memory_order_acq_rel);
        }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        ~Guard() { owner_.active_.fetch_sub(1, std::memory_order_acq_rel); }
    private:
        CallbackCounter& owner_;
    };

    Guard Enter() noexcept { return Guard(*this); }
    unsigned Active() const noexcept { return active_.load(std::memory_order_acquire); }

private:
    std::atomic<unsigned> active_{0};
};

} // namespace hooks
