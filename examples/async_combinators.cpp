#include <cstdio>
#include "../include/xnet.hpp"

template<bool ret_error = false>
xnet::task<xnet::io_result<int>> coro(xnet::io_context& ctx, int timed);

xnet::task<> demo_any(xnet::io_context& ctx);
xnet::task<> demo_race(xnet::io_context& ctx);
xnet::task<> demo_all(xnet::io_context& ctx);
xnet::task<> demo_allSettled(xnet::io_context& ctx);
xnet::detached_task run_all_demos(xnet::io_context& ctx);
void my_scheduler(xnet::io_context& ctx);

int main() {
    xnet::io_context ctx(128);
    run_all_demos(ctx);
    my_scheduler(ctx);
    return 0;
}

xnet::detached_task run_all_demos(xnet::io_context& ctx){
    co_await demo_any(ctx);
    co_await demo_race(ctx);
    co_await demo_all(ctx);
    co_await demo_allSettled(ctx);
}

template<bool ret_error>
xnet::task<xnet::io_result<int>> coro(xnet::io_context& ctx, int timed){
    printf("start working...\n");
    xnet::AsyncTimer timer(ctx);
    auto success = co_await timer.timeout(timed);
    if(!success){
        co_return {0, success.err};
    }
    co_return {100 * timed, ret_error ? ECANCELED : 0};
}

// resume as the first successes
xnet::task<> demo_any(xnet::io_context& ctx){
    printf("------------------------\n");
    printf("[demo_any]: test begin\n");
    auto coro1 = coro<true>(ctx, 5);
    auto coro2 = coro<>(ctx, 7);
    auto coro3 = coro<>(ctx, 9);
    auto coro4 = coro<>(ctx, 11);
    auto result = co_await xnet::any(coro1, coro2, coro3, coro4);
    printf("[demo_any]: who return: %d, result: %d\n", result.who(), *result.get<1>()); // 1, 700
}

// resume as the first completes(success or failed)
xnet::task<> demo_race(xnet::io_context& ctx){
    printf("------------------------\n");
    printf("[demo_race]: test begin\n");
    auto coro1 = coro<true>(ctx, 5);
    auto coro2 = coro<>(ctx, 7);
    auto coro3 = coro<>(ctx, 9);
    auto coro4 = coro<>(ctx, 11);
    auto result = co_await xnet::race(coro1, coro2, coro3, coro4);
    printf("[demo_race]: who return: %d, result: %d\n", result.who(), *result.get<0>()); // 0, 500
}

// resume when all completes or one fails()
xnet::task<> demo_all(xnet::io_context& ctx){
    printf("------------------------\n");
    printf("[demo_all]: test begin\n");
    auto coro1 = coro<>(ctx, 5);
    auto coro2 = coro<true>(ctx, 7);
    auto coro3 = coro<>(ctx, 9);
    auto coro4 = coro<>(ctx, 11);
    auto& [r1, r2, r3, r4] = co_await xnet::all(coro1, coro2, coro3, coro4);
    printf("[demo_all] results: r1-%d, r2-%d, r3-%d, r4-%d\n", *r1, *r2, *r3, *r4); // 500, 0, 0, 0
}

xnet::task<> demo_allSettled(xnet::io_context& ctx){
    printf("------------------------\n");
    printf("[demo_allSettled]: test begin\n");
    auto coro1 = coro<>(ctx, 5);
    auto coro2 = coro<true>(ctx, 7);
    auto coro3 = coro<>(ctx, 9);
    auto coro4 = coro<>(ctx, 11);
    auto& [r1, r2, r3, r4] = co_await xnet::allSettled(coro1, coro2, coro3, coro4);
    printf("[demo_allSettled] results: r1-%d, r2-%d, r3-%d, r4-%d\n", *r1, *r2, *r3, *r4); // 500, 700, 900, 1100
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
