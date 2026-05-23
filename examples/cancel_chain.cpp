#include"../include/xnet.hpp"
#include<cstdio>

xnet::io_context ctx;
xnet::AsyncTimer timer(ctx);

using CancelToken = xnet::task<int>::CancelToken;

xnet::detached_task cancel_after_time(CancelToken token, int timed) noexcept{
    co_await timer.timeout(timed);
    printf("try to cancel now...\n");
    (void)token.cancel();
}

template<class T>
xnet::detached_task cancel_after_time(T&& task, int timed) noexcept{
    co_await timer.timeout(timed);
    printf("try to cancel now...\n");
    (void)task.cancel();
}

xnet::task<double> node3(){
    printf("node3 start\n");
    auto result = co_await timer.timeout(2);
    printf("node3 wakeup\n");
    if(result.err){
        printf("node3 Error: %d\n", result.err);
    }
    printf("node3 done\n");
    co_return 333.3;
}

xnet::task<int> node2(){
    printf("node2 start\n");
    auto result = co_await timer.timeout(2);
    printf("node2 wakeup\n");
    if(result.err){
        printf("node2 Error: %d\n", result.err);
    }
    auto task = node3();
    (void)task.cancel();
    co_await task;
    co_await node3();
    printf("node2 done\n");
    co_return -2;
}

xnet::task<> node1(){
    printf("node1 start\n");
    auto task = node2();
    // cancel_after_time(task, 10); // dead
    // cancel_after_time(task, 2); // well, not dead but still not safe

    cancel_after_time(task.cancel_token(), 10); // safe!
    // cancel_after_time(task.cancel_token(), 2);
    co_await task;
    printf("node1 done\n");
}


int main(){
    xnet::fire(node1());
    ctx.run_until_complete();
    return 0;
}