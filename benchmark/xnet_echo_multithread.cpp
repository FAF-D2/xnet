#include<thread>
#include"../include/xnet.hpp"

const char* ip = "127.0.0.1";
uint16_t port = 52333;
constexpr size_t num_threads = 4;
constexpr size_t CQE_BATCH = 1024;
constexpr size_t SQE_BATCH = 1024;

xnet::detached_task echo(xnet::TCPServer conn){
    char buf[256];
    xnet::io_result<size_t> result;
    size_t bytes;
    while(true){
        result = co_await conn.recv(buf, sizeof(buf), 0);
        if(result.err || *result == 0){
            break;
        }
        bytes = *result;
        result = co_await conn.send(buf, bytes, MSG_WAITALL);
        if(result.err || *result != bytes){
            break;
        }
    }
}

xnet::task<> accept_loop(xnet::io_context& ctx){
    xnet::TCPAccepter accepter(ctx, xnet::v4addr(ip, port), true);

    while(true){
        auto result = co_await accepter.accept();
        if(result.err){
            continue;
        }
        echo(result.move());
    }
}

void my_scheduler(xnet::io_context& ctx){
    io_uring* ring = ctx.native();
    io_uring_cqe* cqes[CQE_BATCH];

    ctx.submit_and_wait();
    while(true){
        unsigned int count = io_uring_peek_batch_cqe(ring, cqes, CQE_BATCH);

        for(unsigned int i = 0; i < count; i++){
            io_uring_cqe* cqe = cqes[i];
            auto handle = std::coroutine_handle<>::from_address(
                (void*)io_uring_cqe_get_data64(cqe)
            );
            if(handle){
                int& result_slot = xnet::io_context::result();
                result_slot = cqe->res;
                handle.resume();
            }
            if((i + 1) % 256 == 0){
                ctx.submit();
            }
        }
        io_uring_cq_advance(ring, count);
        ctx.submit_and_wait();
    }
}

void worker_thread(size_t id){
    (void)id;
    xnet::io_context ctx(SQE_BATCH);

    auto main_coro = accept_loop(ctx);
    main_coro.start();

    my_scheduler(ctx);
}

template<size_t... I>
void start_all_threads(std::index_sequence<I...>){
    std::thread threads[num_threads - 1] = {
        std::thread(worker_thread, I)...
    };
    worker_thread(num_threads - 1);
    for(size_t i = 0; i < num_threads - 1; i++){
        threads[i].join();
    }
}

int main(){
    start_all_threads(std::make_index_sequence<num_threads - 1>());
    return 0;
}