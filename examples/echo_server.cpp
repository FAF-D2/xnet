#include "../include/xnet.hpp"
#include <cstdio>

const char* ip = "127.0.0.1";
uint16_t port = 52333;

xnet::detached_task echo(xnet::TCPServer conn);
xnet::task<> accept_loop(xnet::io_context& ctx);
xnet::detached_task monitor(xnet::io_context& ctx, int interval = 10);
void my_scheduler(xnet::io_context& ctx);

int main() {
    xnet::io_context ctx(256);

    auto main_coro = accept_loop(ctx);
    main_coro.start();   // run until first suspend
    monitor(ctx, 10);    // fire the monitor coroutine
    my_scheduler(ctx);

    return 0;
}

xnet::detached_task monitor(xnet::io_context& ctx, int interval){
    xnet::AsyncTimer timer(ctx);
    while(true){
        auto success = co_await timer.timeout(interval);
        printf("[Monitor]: %ld tasks in wait queue\n", ctx.num_evs());
    }
}

xnet::detached_task echo(xnet::TCPServer conn) {
    char buf[64];
    xnet::io_result<size_t> result; // for unified error handling
    while (true) {
        result = co_await conn.recv(buf, sizeof(buf), 0).timeout(60);
        if (result.err || *result == 0) {
            break;
        }

        result = co_await conn.send(buf, *result, 0);
        if (result.err) {
            break;
        }
    }
    // error handling
    if(result.err){
        printf("Error: %d\n", result.err);
    }
    else if(*result == 0){
        printf("client closed\n");
    }
}

xnet::task<> accept_loop(xnet::io_context& ctx) {
    // using myAccepter = xnet::v4TCPAccepter;
    using myAccepter = xnet::TCPAccepter;
    myAccepter accepter(ctx, xnet::v4addr(ip, port));
    printf("Echo Server starts at [%s:%d]\n", ip, port);

    while (true) {
        auto result = co_await accepter.accept();
        if (result.err) {
            printf("accept error: %d\n", result.err);
            continue;
        }
        echo(result.move()); // spawn echo coroutine
    }
}


void my_scheduler(xnet::io_context& ctx){
    io_uring* ring = ctx.native();
    constexpr size_t CQE_BATCH = 64;
    io_uring_cqe* cqes[CQE_BATCH];

    printf("Scheduler starts\n");
    while (true) {
        unsigned int count = io_uring_peek_batch_cqe(ring, cqes, CQE_BATCH);

        for (unsigned int i = 0; i < count; i++) {
            io_uring_cqe* cqe = cqes[i];
            auto handle = std::coroutine_handle<>::from_address(
                (void*)io_uring_cqe_get_data64(cqe)
            );

            if (handle) {
                int& result_slot = xnet::io_context::result();
                result_slot = cqe->res;
                handle.resume();
            }
        }

        io_uring_cq_advance(ring, count);
        ctx.submit();
    }
}
