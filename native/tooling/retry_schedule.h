#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <type_traits>
#include <utility>
namespace history {
// Deadlines use the caller's monotonic millisecond clock. Equivalent keys keep
// insertion order, so retry delays cannot hide an earlier due job behind them.
template<class Job> class RetrySchedule {
    static_assert(std::is_nothrow_move_constructible<Job>::value,
                  "Retry jobs must transfer ownership without throwing");
    std::multimap<uint64_t,Job> jobs_;
public:
    // Node allocation precedes the noexcept ownership transfer. On allocation
    // failure the caller still owns job and can retain it for another attempt.
    void Push(Job&& job,uint64_t deadline_ms) {
        jobs_.emplace(deadline_ms,std::move(job));
    }
    std::optional<Job> PopDue(uint64_t now_ms) {
        const auto first=jobs_.begin();
        if(first==jobs_.end()||first->first>now_ms)return std::nullopt;
        std::optional<Job> result(std::move(first->second));
        jobs_.erase(first);return result;
    }
    std::optional<uint64_t> NextDeadline() const noexcept {
        if(jobs_.empty())return std::nullopt;
        return jobs_.begin()->first;
    }
};
}
