#include <thread>
#include <vector>
#include <array>

namespace asio::detail {
    template <typename Exception>
    void throw_exception(const Exception&) {
        std::abort();
    }
}

#include "./asio/asio/include/asio.hpp"

using namespace asio;

constexpr uint16_t port = 52333;
constexpr size_t num_threads = 4;
constexpr size_t buf_size = 256;

namespace this_coro = asio::this_coro;
using asio::ip::tcp;
using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::use_awaitable;

awaitable<void> echo(tcp::socket socket)
{
    socket.set_option(tcp::no_delay(true));
    char data[buf_size];
    for (;;) {
        std::size_t n = co_await socket.async_read_some(asio::buffer(data), use_awaitable);
        co_await async_write(socket, asio::buffer(data, n), use_awaitable);
    }
}

awaitable<void> listener(asio::io_context& ctx){
    tcp::acceptor acceptor(ctx);
    tcp::endpoint ep(tcp::v4(), port);
    acceptor.open(ep.protocol());

    int opt = 1;
    setsockopt(acceptor.native_handle(), SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    acceptor.bind(ep);
    acceptor.listen(asio::socket_base::max_listen_connections);

    for(;;){
        tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
        co_spawn(ctx, echo(std::move(socket)), detached);
    }
}

void worker_thread(size_t id){
    (void)id;
    asio::io_context ctx(1);
    co_spawn(ctx, listener(ctx), detached);
    ctx.run();
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