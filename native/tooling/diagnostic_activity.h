#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace history::diagnostics {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free&&std::atomic<unsigned>::is_always_lock_free,
    "Diagnostic producers require native lock-free integer atomics");
enum class Phase : unsigned {MenuCreate,MenuSubmenu,MenuFormat,MenuWillShow,MenuExecute,MenuClosed,StartupHooks};
inline const char* PhaseName(Phase phase) noexcept {
    switch(phase) {
        case Phase::MenuCreate:return "menu-create";case Phase::MenuSubmenu:return "menu-submenu";
        case Phase::MenuFormat:return "menu-format";case Phase::MenuWillShow:return "menu-show";
        case Phase::MenuExecute:return "menu-execute";case Phase::MenuClosed:return "menu-close";
        case Phase::StartupHooks:return "hook-activation";
    }
    return "unknown";
}
struct ActivityToken {
    std::size_t index=32;
    std::uint64_t generation=0,started=0;
    bool Valid() const noexcept {return index<32&&generation>=2;}
};
struct ActivityObservation {
    Phase phase;
    bool pending;
    unsigned thread;
    std::uint64_t started_ms,elapsed_ms;
};
// Producers own a slot until Finish. State 0=free, 1=being initialized,
// even generation=active, odd generation=completed. A unique generation stops
// stale readers/tokens from confusing successive uses of the same slot.
// All fields are atomic: the consumer may read while Finish or reuse occurs.
class ActivityTracker {
    struct Slot {
        std::atomic<std::uint64_t> state{0},started{0},ended{0};
        std::atomic<unsigned> phase{0},thread{0};
    };
public:
    static constexpr std::size_t capacity=32;
private:
    std::array<Slot,capacity> slots_{};
    std::atomic<std::uint64_t> generation_{2},omitted_{0};
    // Only the logger worker calls Poll / owns these rate-limit fields.
    std::array<std::uint64_t,capacity> reported_generation_{},reported_at_{};
public:
    ActivityToken Begin(Phase phase,unsigned thread,std::uint64_t now) noexcept {
        for(std::size_t i=0;i<slots_.size();++i) {
            auto& slot=slots_[i];std::uint64_t free=0;
            if(!slot.state.compare_exchange_strong(free,1,std::memory_order_acq_rel))continue;
            const auto generation=generation_.fetch_add(2,std::memory_order_relaxed);
            if(generation<2){slot.state.store(0,std::memory_order_release);break;}
            slot.started.store(now,std::memory_order_relaxed);slot.ended.store(0,std::memory_order_relaxed);
            slot.phase.store(unsigned(phase),std::memory_order_relaxed);slot.thread.store(thread,std::memory_order_relaxed);
            slot.state.store(generation,std::memory_order_release);return {i,generation,now};
        }
        omitted_.fetch_add(1,std::memory_order_relaxed);return {};
    }
    void Finish(const ActivityToken& token,std::uint64_t now) noexcept {
        if(!token.Valid())return;
        auto& slot=slots_[token.index];
        if(slot.state.load(std::memory_order_acquire)!=token.generation)return;
        if(now<token.started||now-token.started<10) {
            slot.state.store(0,std::memory_order_release);return;
        }
        slot.ended.store(now,std::memory_order_relaxed);
        slot.state.store(token.generation|1,std::memory_order_release);
    }
    template<class Emit> void Poll(std::uint64_t now,Emit emit) {
        for(std::size_t i=0;i<slots_.size();++i) {
            auto& slot=slots_[i];auto state=slot.state.load(std::memory_order_acquire);
            if(state<2)continue;
            const bool pending=(state&1)==0;
            const auto started=slot.started.load(std::memory_order_relaxed);
            const auto ended=pending?now:slot.ended.load(std::memory_order_relaxed);
            const auto phase=Phase(slot.phase.load(std::memory_order_relaxed));
            const auto thread=slot.thread.load(std::memory_order_relaxed);
            if(slot.state.load(std::memory_order_acquire)!=state||ended<started)continue;
            const auto duration=ended-started;
            if(pending) {
                if(duration<250)continue;
                if(reported_generation_[i]==state&&now-reported_at_[i]<5000)continue;
                reported_generation_[i]=state;reported_at_[i]=now;
            }else if(!slot.state.compare_exchange_strong(state,0,std::memory_order_acq_rel))continue;
            emit(ActivityObservation{phase,pending,thread,started,duration});
        }
    }
    std::uint64_t TakeOmitted() noexcept {return omitted_.exchange(0,std::memory_order_relaxed);}
};
enum class ProgressState {None,Stalled,Recovered};
class ProgressWatch {
    bool stalled_=false;
    std::uint64_t reported_=0;
public:
    ProgressState Observe(std::uint64_t progress,std::uint64_t now) noexcept {
        if(!progress){stalled_=false;reported_=0;return ProgressState::None;}
        if(now>=progress&&now-progress>=1000) {
            if(stalled_&&now>=reported_&&now-reported_<5000)return ProgressState::None;
            stalled_=true;reported_=now;return ProgressState::Stalled;
        }
        if(stalled_){stalled_=false;return ProgressState::Recovered;}
        return ProgressState::None;
    }
};
}
