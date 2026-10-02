#pragma once

#include <atomic>
#include <cstdint>

namespace hooks {

enum class InitState : unsigned char {
    Waiting,
    Installing,
    Active,
    Unsupported,
    RetryableFailure,
};

class InitController {
public:
    explicit InitController(std::uint64_t retry_delay_ms = 1000) noexcept
        : retry_delay_ms_(retry_delay_ms) {}

    bool TryBegin(std::uint64_t now_ms) noexcept {
        InitState observed = state_.load(std::memory_order_acquire);
        for (;;) {
            if (observed == InitState::RetryableFailure &&
                now_ms < retry_after_ms_.load(std::memory_order_acquire)) {
                return false;
            }
            if (observed != InitState::Waiting &&
                observed != InitState::RetryableFailure) {
                return false;
            }
            if (state_.compare_exchange_weak(observed, InitState::Installing,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
                return true;
            }
        }
    }

    void Activate() noexcept {
        state_.store(InitState::Active, std::memory_order_release);
    }

    void MarkUnsupported() noexcept {
        state_.store(InitState::Unsupported, std::memory_order_release);
    }

    void Retry(std::uint64_t now_ms) noexcept {
        retry_after_ms_.store(now_ms + retry_delay_ms_, std::memory_order_release);
        state_.store(InitState::RetryableFailure, std::memory_order_release);
    }

    InitState State() const noexcept {
        return state_.load(std::memory_order_acquire);
    }

private:
    const std::uint64_t retry_delay_ms_;
    std::atomic<std::uint64_t> retry_after_ms_{0};
    std::atomic<InitState> state_{InitState::Waiting};
};

} // namespace hooks
