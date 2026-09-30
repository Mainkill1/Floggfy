#pragma once
#include <array>
#include <optional>
#include <mutex>
#include <utility>
namespace history {
template<class T,size_t N> class BoundedQueue {
 std::mutex mutex_;std::array<std::optional<T>,N> slots_;size_t head_=0,tail_=0,count_=0;
public:
 bool Push(T&& item){std::unique_lock<std::mutex> l(mutex_,std::try_to_lock);if(!l||count_==N)return false;slots_[tail_].emplace(std::move(item));tail_=(tail_+1)%N;++count_;return true;}
 bool Pop(T& item){std::unique_lock<std::mutex> l(mutex_,std::try_to_lock);if(!l||!count_)return false;item=std::move(*slots_[head_]);slots_[head_].reset();head_=(head_+1)%N;--count_;return true;}
};
}
