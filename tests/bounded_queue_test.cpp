#include "../native/bounded_queue.h"
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>

struct BlockingMove {
 int value=0;bool block=false;
 static std::promise<void>* entered;
 static std::shared_future<void>* release;
 BlockingMove()=default;BlockingMove(int v,bool b=false):value(v),block(b){}
 BlockingMove(BlockingMove&&)=default;
 BlockingMove& operator=(BlockingMove&& other){
  value=other.value;block=other.block;
  if(block&&entered&&release){entered->set_value();release->wait();}
  return *this;
 }
 BlockingMove(const BlockingMove&)=delete;BlockingMove& operator=(const BlockingMove&)=delete;
};
std::promise<void>* BlockingMove::entered=nullptr;
std::shared_future<void>* BlockingMove::release=nullptr;

int main(){
 history::BoundedQueue<int,2> q;assert(q.Push(1)&&q.Push(2)&&!q.Push(3));int value=0;assert(q.Pop(value)&&value==1);
 assert(q.Push(4)&&!q.Push(5));assert(q.Pop(value)&&value==2&&q.Pop(value)&&value==4&&!q.Pop(value));

 history::BoundedQueue<BlockingMove,2> contended;
 BlockingMove source(7,true);assert(contended.Push(std::move(source)));
 std::promise<void> entered,release;auto gate=release.get_future().share();
 BlockingMove::entered=&entered;BlockingMove::release=&gate;
 auto consumer=std::async(std::launch::async,[&]{BlockingMove out;return contended.Pop(out)&&out.value==7;});
 entered.get_future().wait();
 std::promise<void> producer_started;
 auto producer=std::async(std::launch::async,[&]{producer_started.set_value();BlockingMove next(8);return contended.Push(std::move(next));});
 producer_started.get_future().wait();
 assert(producer.wait_for(std::chrono::milliseconds(50))==std::future_status::timeout);
 release.set_value();assert(consumer.get()&&producer.get());
 BlockingMove result;assert(contended.Pop(result)&&result.value==8);
 std::cout<<"PASS: bounded publication queue preserves completed work through lock contention\n";
}
