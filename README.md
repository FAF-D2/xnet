# xnet

## About this project
xnet is a **lightweight, ~3k-line, header-only** async I/O layer that exposes minimal awaiters built on **io_uring (via liburing)** and **C++20 coroutines**.

It does **one thing only**: fast, minimal, scheduler‑agnostic asynchronous I/O — with each IOAwaiter costing only **32–64 bytes** of overhead.

**No scheduler, no runtime — just the minimal I/O primitives you can drop directly into your own coroutine system.*

## Quickstart

xnet is header-only, so there is nothing to build.  
Just drop `include/xnet.hpp` into your project and add the include path.

```cpp
#include"xnet.hpp"
```

BUT you will also need:
- Linux kernel **5.15+** (Recommended 5.17+)
- `liburing-dev`
- A C++20 compiler

if liburing not installed: **Ubuntu / Debian**
```bash
sudo apt update
sudo apt install liburing-dev
```

## Benchmark

This benchmark simulates a realistic high‑fanout service: thousands of long‑lived TCP connections, each sending requests at irregular intervals.

**This load pattern stresses**:
+ *event‑loop stability*
+ *coroutine scheduling overhead (p50)*
+ *tail‑latency behavior (p99)*
+ *throughput under jittery workloads*

**Testing environment**: 
+ *Linux 6.17.0-1007-aws*
+ *AWS m7i-flex.large (2 vCPU, 8 GB RAM)*
+ *g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0*

**Testing method:**

Each client sends a 500‑byte echo request every 0–20 ms (uniform random), measuring the RTT and reporting the p50 and p99 ($P^2$ *quantile estimator*).

**Result:**

*(1k clients, workers = 10, num_clients = 100)*
| Library | p50 | p99 | QPS |
|--------|------|-------|--------|
| **xnet** | **3.6 ms** | **151.8 ms** | **67,130** |
| Asio | 7.11 ms | 177.07 ms | 56,540 |
| Go | 7.81 ms | 172.4 ms | 55,438 |

*(10k clients, workers = 100, num_clients = 100)*
| Library | p50 | p99 | QPS |
|--------|------|-------|--------|
| **xnet** | **235.57 ms** | **303.257 ms** | **42,133** |
| Asio | 249.524 ms | 318.82 ms | 40,876 |
| Go | 309.681 ms |  311.7 ms | 41,056 |




## Some Examples
*See more examples in the [examples/](./examples/) directory.*

A echo implementation is simple as follows:

```cpp
xnet::detached_task echo(xnet::TCPServer conn){
    char buf[64];
    while(true){
        auto n = co_await conn.recv(buf, sizeof(buf), 0).timeout(60);
        if (!n || *n == 0) break; // closed

        co_await conn.send(buf, *n, 0);
    }
}

xnet::task<> accept_loop(xnet::io_context& ctx) {
    xnet::io_context::TCPAccepter accepter(ctx, xnet::v4addr("127.0.0.1", 52333));

    while (true) {
        auto conn = co_await accepter.accept();
        echo(conn.move()); // spawn echo coroutine
    }
}
```
You will need to write your own scheduler as xnet do not assume any schedular scenario *(multi-threads, single-thread, etc.)*. A possible implementation is:

For simplicity use, or you can just use ctx.run_until_complete() for a basic scheduler
```cpp
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

int main(){
    xnet::io_context ctx(128);
    auto coro = accept_loop(ctx);
    coro.start() // start the coroutine until co await

    // scheduler
    my_scheduler();
    // or
    // ctx.run_until_complete();
}
```

Also, xnet provides some async combinators (all, allSettled, any, race) that can simply group the coroutine

```cpp
auto result = co_await xnet::race(coro1, coro2, coro3);
auto result = co_await xnet::any(coro1, coro2, coro3);
auto result = co_await xnet::all(coro1, coro2, coro3);
auto result = co_await xnet::allSettled(coro1, coro2, coro3);
```


## xnet contract
*xnet does not ship a scheduler. Instead, it defines a minimal contract between your run loop and the awaiters*.

### Here are the core behaviors helping you define your high-spec scheduler:

___
1. When you `co_await` an I/O operation (e.g. `co_await accepter.accept()`), the
awaiter only prepares an SQE. It does **not** submit it.  
Your scheduler must call `io_context.submit()` at a suitable time to flush all pending SQEs.

**If you do not call `submit()`, you will never receive a CQE and the coroutine will never resume.**
___
2. If you want to define your own scheduler, when a CQE arrives, write `cqe->res` into the thread‑local result slot `io_context::result()` **immediately before** `resume()`. It acts as the register value that will be readed immediately by the IOAwaiter in `await_resume()`

```cpp
    io_uring_cqe* cqe = cqes[i];
    std::coroutine_handle<> handle = std::coroutine_handle<>::from_address((void*)io_uring_cqe_get_data64(cqe));
    int& result_slot = io_context::result(); // static thread_loacal

    if(handle){
        result_slot = cqe->res; // must do before resume
        handle.resume();
    }
```
___
3. The result of `co_await IOAwaiter` is a result type io_result\<T\> consisting of T value and int err. Please be aware of possible error handling when using the xnet.
```cpp
auto result = co_await conn.recv(buf, sizeof(buf), 0);
if(result.err) // err handling
size_t readed_bytes = *result;
```

4. Always turn on the MACRO `XNET_DISABLE_THREAD_SAFE` if you do not use io_context and any tasks across threads

## API Overview

### Core type
1. `xnet::io_context`  
Owns the io_uring instance and provides `submit()`,  `result()`, and all interfaces needed for any scheduler.

2. `xnet::io_result<T>`  
Result type returned by all IO awaiters: { T value, int err }.
Use *result or result->func() to access the value when err == 0.

3. `xnet::task<T> / xnet::detached_task / xnet::ptask<T>`  
Coroutine types for user coroutines. detached_task is a fire & forget coroutine, ptask is the pure task type if you dont care about cancellation

4. `IOSender type`
    + `TCPAccepter`, `AsyncTimer`: e.g. co_await accepter.accept() -> `io_result<TCPServer>`
    + IOStream: `TCPServer`, `STDIN`, `AsyncFile`, `TCPClient` etc. , `co await` recv()/send() -> io_result<size_t>
    + **! `rebind_context(io_context&)`**, change the sender execution context.
    
        **All IOAwaiters derived from this sender will register the new context sqe when co_await**  

5. Async combinators `any`, `race`, `all`, `allSettled`

6. Safe Cancellation

See `examples/cancel_chain.cpp` for more details

```cpp
xnet::task<> coro(){
    // ...
}

auto task = coro();
auto token = task.cancel_token();
// pass token to other coroutines or threads
co_await task;
```

## Cancellation mechanism
xnet provides the ref-count model for cancellation in task type. If you do care about overhead, use `xnet::ptask` instead.
The overhead of hooking cancellation chain is 40-50 cycles (~20ns) in thread_safe mode each `await_suspend`.

Each IOAwaiter will write their cancel fatpointer in a common-space shared by all nodes in the coroutines tree and then modify task state in the call chain in the recursion. So the cancellation of a task will not affect the whole tree but only the chain in its fanouts.

The cancellation usage:
```cpp
xnet::io_context ctx;
xnet::AsyncTimer timer(ctx);

template<class T>
xnet::detached_task cancel_after_time(T token, int timed){
    co_await timer.timeout(timed);
    auto ret = token.cancel();
}

xnet::task<size_t> func(xnet::TCPServer s){
    auto task = some_task();
    cancel_after_time(task.cancel_token(), 10);
    co_await task;
}
```

The return value of `cancel()` is io_result\<bool\>:
+ {true, 0} cancel signal sended, will cancel the actual IO if it is not completed
+ {false, EAGAIN}  the coroutine is not suspended or suspended failed (Get SQE error)
+ {false, ECANCELLED} (the coroutine is already finished)

Implement a correct task type with correct cancellation implementation is hard, so it is recommended that use the `task<>` directly if you need cancellation or `ptask<>` if you do not care about the cancellation.