#include "../native/bounded_queue.h"
#include <cassert>
#include <future>
#include <iostream>
int main(){
 history::BoundedQueue<int,2> q;assert(q.Push(1)&&q.Push(2)&&!q.Push(3));int value=0;assert(q.Pop(value)&&value==1);
 std::promise<void> blocked,release;auto gate=release.get_future();
 auto slow=std::async(std::launch::async,[&]{int v;assert(q.Pop(v)&&v==2);blocked.set_value();gate.wait();});blocked.get_future().wait();
 assert(q.Push(4)&&q.Push(5)&&!q.Push(6));assert(q.Pop(value)&&value==4);
 release.set_value();slow.get();assert(q.Pop(value)&&value==5&&!q.Pop(value));
 std::cout<<"PASS: bounded publication queue remains usable while consumer operation is blocked\n";
}
