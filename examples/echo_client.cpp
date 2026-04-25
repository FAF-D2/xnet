#include "../include/xnet.hpp"
#include <cstdio>

const char* ip = "127.0.0.1";
uint16_t port = 52333;
constexpr size_t num_clients = 10;
constexpr size_t resend_times = 10;

xnet::detached_task client_session(xnet::io_context& ctx, int id, int timed);
void my_scheduler(xnet::io_context& ctx);

int main() {
    xnet::io_context ctx(128);

    for(size_t i = 0; i < num_clients; i++){
        client_session(ctx, i, i + 1); // fire {num_clients} coroutines
    }

    my_scheduler(ctx);
    return 0;
}

xnet::detached_task client_session(xnet::io_context& ctx, int id, int timed){
    xnet::TCPClient client(ctx);
    xnet::AsyncTimer timer(ctx);

    printf("[Client %d]: Connecting to %s:%d...\n", id, ip, port);
    
    auto addr = xnet::v4addr(ip, port);
    auto success = co_await client.connect((const sockaddr*)&addr, sizeof(addr));

    if(/* !success */ success.err){
        printf("[Client %d]: connect error: %d\n", id, success.err);
        co_return;
    }
    printf("[Client %d]: Connected!\n", id);
    
    xnet::io_result<size_t> result;
    char buf[8];
    for(size_t i = 0; i < resend_times; i++){
        co_await timer.timeout(timed);

        printf("[Client %d]: sending \"hello\"...\n", id);
        result = co_await client.send("hello\n", 6, 0);
        if (result.err) {
            break;
        }

        result = co_await client.recv(buf, sizeof(buf), 0);
        if (result.err || *result != 6) {
            break;
        }
        buf[*result] = '\0';
        printf("[Client %d]: Recv msg %s", id, buf);
    }
    
    if (result.err) {
        printf("[Client %d]: recv error %d\n", id, result.err);
    }
    else if (*result == 0) {
        printf("[Client %d]: server closed\n", id);
    }
    else{
        printf("[Client %d]: return with no error\n", id);
    }
}

void my_scheduler(xnet::io_context& ctx){
    io_uring* ring = ctx.native();
    constexpr size_t CQE_BATCH = 64;
    io_uring_cqe* cqes[CQE_BATCH];

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
