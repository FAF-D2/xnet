#include<cstdlib>
#include<cstdio>
#include<cstring>
#include<chrono>
#include<random>
#include<unistd.h>
#include<sys/wait.h>
#include<sys/mman.h>
#include"xnet.hpp"

const char* ip = "127.0.0.1";
uint16_t port = 52333;

constexpr int workers = 10;
constexpr int num_clients = 100;
constexpr int payload_size = 500;
constexpr int duration = 60;

size_t query_counter = 0;
size_t failed = 0;
constexpr size_t CQE_BATCH = 1024;
constexpr size_t SQE_BATCH = 1024;

static auto addr = xnet::v4addr(ip, port);
constexpr socklen_t addr_size = sizeof(addr);

struct P2Quantile{
    double q;
    bool initialized = false;
    long n = 0;
    
    double m[5];
    double pos[5];
    double np[5];
    double dn[5];

    explicit P2Quantile(double q): q(q){}

    void add(double x){
        if(!initialized){
            init_with_sample(x);
            return;
        }
        ++n;
        update_markers(x);
    }

    double value() const{
        return initialized ? m[2] : 0.0;
    }
private:
    void init_with_sample(double x){
        static double buf[5];
        static int cnt = 0;
        buf[cnt++] = x;
        if(cnt < 5) return;
        std::sort(buf, buf + 5);
        for(int i = 0; i < 5; i++){
            m[i] = buf[i];
            pos[i] = i + 1;
        }
        n = 5;
        np[0] = 1;
        np[1] = 1 + 2 * q;
        np[2] = 1 + 4 * q;
        np[3] = 3 + 2 * q;
        np[4] = 5;

        dn[0] = 0;
        dn[1] = q / 2;
        dn[2] = q;
        dn[3] = (1 + q) / 2;
        dn[4] = 1;

        initialized = true;
    }

    void update_markers(double x) {
        int k;
        if (x < m[0]) {
            m[0] = x;
            k = 0;
        } else if (x >= m[4]) {
            m[4] = x;
            k = 3;
        } else {
            for (k = 0; k < 4; ++k) {
                if (x < m[k+1]) break;
            }
        }

        for (int i = k + 1; i < 5; ++i)
            pos[i] += 1;

        for (int i = 0; i < 5; ++i)
            np[i] += dn[i];

        for (int i = 1; i < 4; ++i) {
            double d = np[i] - pos[i];
            if ((d >= 1 && pos[i+1] - pos[i] > 1) ||
                (d <= -1 && pos[i-1] - pos[i] < -1)) {

                int sign = (d > 0) ? 1 : -1;

                double p = pos[i] - pos[i-1];
                double qd = pos[i+1] - pos[i];
                double a = (m[i+1] - m[i]) / qd;
                double b = (m[i] - m[i-1]) / p;
                double delta = sign * ((p + sign) * a + (qd - sign) * b) / (qd + p);

                double new_m = m[i] + delta;
                if (new_m > m[i-1] && new_m < m[i+1]) {
                    m[i] = new_m;
                } else {
                    // fallback to linear
                    m[i] += sign * (m[i + sign] - m[i]) / (pos[i + sign] - pos[i]);
                }
                pos[i] += sign;
            }
        }
    }
};

P2Quantile p50(0.5);
P2Quantile p99(0.99);

struct worker_result{
    double p50;
    double p99;
    size_t queries;
    size_t failed;
};

xnet::detached_task client_task(xnet::io_context& ctx, int id){
    xnet::TCPClient client(ctx);
    xnet::AsyncTimer timer(ctx);

    std::uniform_int_distribution<int> dist(0, 20);
    std::mt19937 rng(id * 1234567);

    char sendbuf[payload_size];
    char recvbuf[payload_size];
    memset(sendbuf, 'A' + (id % 26), payload_size);

    xnet::io_result<size_t> result;
    result = co_await client.connect((const sockaddr*)&addr, addr_size);
    co_await timer.timeout(1);
    while(result.err){
        ++failed;
        co_await timer.timeout(1);
        result = co_await client.connect((const sockaddr*)&addr, addr_size);
    }

    while(true){
        while(true){
            auto start = std::chrono::steady_clock::now();

            auto& [r1, r2] = co_await xnet::all(
                client.send(sendbuf, payload_size, 0),
                client.recv(recvbuf, payload_size, MSG_WAITALL)
            );
            
            auto end = std::chrono::steady_clock::now();
            double us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

            p50.add(us);
            p99.add(us);

            if((r1.err || r2.err) || (*r1 == 0 || *r2 == 0)){
                break;
            }
            if(memcmp(recvbuf, sendbuf, payload_size) != 0) break;
            ++query_counter;

            int ms = dist(rng);
            int ns = ms * 1000 * 1000;
            co_await timer.timeout(0, ns);
        }
        ++failed;
        client.close();
        co_await timer.timeout(1);
        result = co_await client.connect((const sockaddr *)&addr, addr_size);
        while(result.err){
            ++failed;
            co_await timer.timeout(1);
            result = co_await client.connect((const sockaddr*)&addr, addr_size);
        }
    }
}

xnet::detached_task watchdog(xnet::io_context& ctx, worker_result* result){
    xnet::AsyncTimer timer(ctx);
    co_await timer.timeout(duration + 1);
    // printf("query count: %ld, failed: %ld\n", query_counter, failed);
    // printf("QPS: %ld\n", size_t(query_counter / duration));
    
    result->p50 = p50.value();
    result->p99 = p99.value();
    result->queries = query_counter;
    result->failed = failed;
    exit(0);
}

void my_scheduler(xnet::io_context& ctx);
void run_load_test(worker_result* result);

int main(){
    worker_result* results = (worker_result*)mmap(
        nullptr, sizeof(worker_result) * workers,
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS, -1, 0
    );
    if(results == MAP_FAILED){
        printf("mmap failed\n");
        return 1;
    }
    memset(results, 0, sizeof(worker_result) * workers);

    pid_t pids[workers];
    for(int i = 0; i < workers; i++){
        pid_t pid = fork();
        if(pid == 0){
            run_load_test(&results[i]);
            exit(0); // never reached
        }
        pids[i] = pid;
    }

    for(int i = 0; i < workers; i++){
        waitpid(pids[i], NULL, 0);
    }

    size_t total_queries = 0;
    size_t total_failed = 0;
    double final_p50 = 0;
    double final_p99 = 0;
    for(int i = 0; i < workers; i++){
        total_queries += results[i].queries;
        total_failed += results[i].failed;
        final_p50 += results[i].p50;
        final_p99 += results[i].p99;
    }
    final_p50 /= workers;
    final_p99 /= workers;
    munmap(results, sizeof(worker_result) * workers);

    printf("p50 = %.2fus, p99 = %.2fus\n", final_p50, final_p99);
    printf("QPS = %ld\n", total_queries / duration);
    return 0;
}

void run_load_test(worker_result* result){
    xnet::io_context ctx(SQE_BATCH);
    for(int i = 0; i < num_clients; i++){
        client_task(ctx, i);
    }
    watchdog(ctx, result);
    my_scheduler(ctx);
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