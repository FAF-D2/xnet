#ifndef xnet_hpp
#define xnet_hpp
#include<cstdint>
#include<tuple>
#include<cerrno>
#include<coroutine>
#include<cstring>
#include<utility>
#include<atomic>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #include <immintrin.h>
    #define XNET_CPU_RELAX() _mm_pause()
#elif defined(__aarch64__) || defined(__arm__)
    #define XNET_CPU_RELAX() asm volatile("yield" ::: "memory")
#else
    #include <thread>
    #define XNET_CPU_RELAX() std::this_thread::yield()
#endif

// #define XNET_DISABLE_THREAD_SAFE
#ifdef XNET_DISABLE_THREAD_SAFE
#pragma message "xnet compiled in thread-unsafe mode"
#endif

#ifdef __linux__
#include<sys/socket.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<sys/un.h>
#include<netinet/tcp.h>
#include<unistd.h>
#include<arpa/inet.h>
#include<fcntl.h>
#include<net/if.h>
#include<ifaddrs.h>
#include<netinet/in.h>
#include<liburing.h>
#include<linux/version.h>

#ifdef XNET_ENABLE_SKIP_SUCCESS
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 17, 0)
#define XNET_ETIME_SUCCESS IORING_TIMEOUT_ETIME_SUCCESS 
#define XNET_SET_FLAGS_SKIP_SUCCESS(sqe) io_uring_sqe_set_flags((sqe), IOSQE_CQE_SKIP_SUCCESS)
#endif
#else
#define XNET_ETIME_SUCCESS 0
#define XNET_SET_FLAGS_SKIP_SUCCESS(sqe)
#endif

namespace xnet{
    static constexpr int INVALID_HANDLE = -1;
    static constexpr int SOCKET_ERROR = -1;

    class io_context;

    namespace details{

#ifdef XNET_DISABLE_THREAD_SAFE
        static constexpr bool THREAD_SAFE_REQUIRED = false;
        template<class T>
        using atomic_type = T;
#else
        static constexpr bool THREAD_SAFE_REQUIRED = true;
        template<class T>
        using atomic_type = std::atomic<T>;
#endif
        template<class T>
        static auto consteval get_awaiter_result(T&& value) {
            if constexpr (requires { operator co_await(std::forward<T>(value)); })
                return (operator co_await(std::forward<T>(value))).await_resume();
            else if constexpr (requires { std::forward<T>(value).operator co_await(); })
                return (std::forward<T>(value).operator co_await()).await_resume();
            else
                return std::forward<T>(value).await_resume();
        }

        template<class T>
        using await_result_t = decltype(get_awaiter_result(std::declval<T>()));

        template<int d, int placeholder>
        struct GET_ADDRTYPE{ using type = sockaddr; };
        template<int placeholder>
        struct GET_ADDRTYPE<AF_INET, placeholder>{ using type = sockaddr_in; };
        template<int placeholder>
        struct GET_ADDRTYPE<AF_INET6, placeholder>{ using type = sockaddr_in6; };
        template<int placeholder>
        struct GET_ADDRTYPE<AF_LOCAL, placeholder>{ using type = sockaddr_un; };

        // fix -Wsubobject-linkage
        inline void wrap_io_uring_prep_poll_remove(io_uring_sqe* sqe, unsigned long long user_data, int flags) noexcept{
            (void)flags;
            return ::io_uring_prep_poll_remove(sqe, user_data);
        }

        inline auto wrap_io_uring_prep_cancel(io_uring_sqe* sqe, void* user_data, int flags) noexcept{
            return ::io_uring_prep_cancel(sqe, user_data, flags);
        }

        inline auto wrap_io_uring_prep_timeout_remove(io_uring_sqe* sqe, unsigned long long user_data, int flags) noexcept{
            return ::io_uring_prep_timeout_remove(sqe, user_data, flags);
        }

        inline auto wrap_io_uring_prep_poll_add(io_uring_sqe* sqe, int fd, unsigned int poll_mask) noexcept{
            return ::io_uring_prep_poll_add(sqe, fd, poll_mask);
        }

        inline auto wrap_io_uring_prep_sendto(io_uring_sqe* sqe, int fd, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr, socklen_t addrlen) noexcept{
            return ::io_uring_prep_sendto(sqe, fd, buf, len, flags, dest_addr, addrlen);
        }

        inline auto wrap_io_uring_prep_sendmsg(io_uring_sqe* sqe, int fd, const struct msghdr* msg, unsigned int flags) noexcept{
            return ::io_uring_prep_sendmsg(sqe, fd, msg, flags);
        }

        inline auto wrap_io_uring_prep_send(io_uring_sqe* sqe, int fd, const void* buf, size_t len, int flags) noexcept{
            return ::io_uring_prep_send(sqe, fd, buf, len, flags);
        }

        inline auto wrap_io_uring_prep_write(io_uring_sqe* sqe, int fd, const void* buf, unsigned int nbytes, __u64 offset) noexcept{
            return ::io_uring_prep_write(sqe, fd, buf, nbytes, offset);
        }

        inline auto wrap_io_uring_prep_writev(io_uring_sqe* sqe, int fd, const struct iovec* iovecs, unsigned int nr_vecs, __u64 offset) noexcept{
            return ::io_uring_prep_writev(sqe, fd, iovecs, nr_vecs, offset);
        }

        inline auto wrap_io_uring_prep_write_fixed(io_uring_sqe* sqe, int fd, const void* buf, unsigned int nbytes, __u64 offset, int buf_index) noexcept{
            return ::io_uring_prep_write_fixed(sqe, fd, buf, nbytes, offset, buf_index);
        }

        inline void wrap_io_uring_prep_sendfile(io_uring_sqe* sqe, int sockfd, int filefd, int64_t offset, unsigned int bytes, unsigned int flags) noexcept{
            ::io_uring_prep_splice(sqe, filefd, offset, sockfd, -1, bytes, flags);
        }

        inline auto wrap_io_uring_prep_read_fixed(io_uring_sqe* sqe, int fd, void* buf, unsigned int nbytes, __u64 offset, int buf_index) noexcept{
            return ::io_uring_prep_read_fixed(sqe, fd, buf, nbytes, offset, buf_index);
        }

        inline auto wrap_io_uring_prep_readv(io_uring_sqe* sqe, int fd, const struct iovec* iovecs, unsigned int nr_vecs, __u64 offset) noexcept{
            return ::io_uring_prep_readv(sqe, fd, iovecs, nr_vecs, offset);
        }

        inline auto wrap_io_uring_prep_recvmsg(io_uring_sqe* sqe, int fd, struct msghdr* msg, unsigned int flags) noexcept{
            return ::io_uring_prep_recvmsg(sqe, fd, msg, flags);
        }

        inline auto wrap_io_uring_prep_recv(io_uring_sqe* sqe, int fd, void* buf, size_t len, int flags) noexcept{
            return ::io_uring_prep_recv(sqe, fd, buf, len, flags);
        }

        inline auto wrap_io_uring_prep_read(io_uring_sqe* sqe, int fd, void* buf, unsigned int nbytes, __u64 offset) noexcept{
            return ::io_uring_prep_read(sqe, fd, buf, nbytes, offset);
        }

        inline auto wrap_io_uring_prep_connect(io_uring_sqe* sqe, int fd, const struct sockaddr* addr, socklen_t addrlen) noexcept{
            return ::io_uring_prep_connect(sqe, fd, addr, addrlen);
        }

        inline auto wrap_io_uring_prep_fsync(io_uring_sqe* sqe, int fd, unsigned int fsync_flags) noexcept{
            return ::io_uring_prep_fsync(sqe, fd, fsync_flags);
        }

        inline auto wrap_io_uring_prep_fallocate(io_uring_sqe* sqe, int fd, int mode, off_t offset, off_t len) noexcept{
            return ::io_uring_prep_fallocate(sqe, fd, mode, offset, len);
        }

        inline auto wrap_io_uring_prep_fadvise(io_uring_sqe* sqe, int fd, off_t offset, off_t len, int advise) noexcept{
            return ::io_uring_prep_fadvise(sqe, fd, offset, len, advise);
        }

        // FS Operations
        inline auto wrap_io_uring_prep_openat(io_uring_sqe* sqe, int dfd, const char* path, int flags, mode_t mode) noexcept{
            return ::io_uring_prep_openat(sqe, dfd, path, flags, mode);
        }

        inline auto wrap_io_uring_prep_renameat(io_uring_sqe* sqe, int olddfd, const char* oldpath, int newdfd, const char* newpath, unsigned int flags) noexcept{
            return ::io_uring_prep_renameat(sqe, olddfd, oldpath, newdfd, newpath, flags);
        }

        inline auto wrap_io_uring_prep_unlinkat(io_uring_sqe* sqe, int dfd, const char* path, int flags) noexcept{
            return ::io_uring_prep_unlinkat(sqe, dfd, path, flags);
        }

        inline auto wrap_io_uring_prep_mkdirat(io_uring_sqe* sqe, int dfd, const char* path, mode_t mode) noexcept{
            return ::io_uring_prep_mkdirat(sqe, dfd, path, mode);
        }

        inline auto wrap_io_uring_prep_statx(io_uring_sqe* sqe, int dfd, const char* path, int flags, unsigned int mask, struct statx* statxbuf) noexcept{
            return ::io_uring_prep_statx(sqe, dfd, path, flags, mask, statxbuf);
        }

        inline auto wrap_io_uring_prep_linkat(io_uring_sqe* sqe, int olddfd, const char* oldpath, int newdfd, const char* newpath, int flags) noexcept{
            return ::io_uring_prep_linkat(sqe, olddfd, oldpath, newdfd, newpath, flags);
        }

        inline auto wrap_io_uring_prep_symlinkat(io_uring_sqe* sqe, const char* target, int newdfd, const char* linkpath) noexcept{
            return ::io_uring_prep_symlinkat(sqe, target, newdfd, linkpath);
        }

        template<auto io_func>
        struct args_traits{};
        template<>
        struct args_traits<wrap_io_uring_prep_poll_add>{
            using args_type = std::tuple<unsigned int>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_sendto>{
            using args_type = std::tuple<const void*, size_t, int, const struct sockaddr*, socklen_t>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_sendmsg>{
            using args_type = std::tuple<msghdr*, unsigned int>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_send>{
            using args_type = std::tuple<const void*, size_t, int>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_write>{
            using args_type = std::tuple<const void*, unsigned, __u64>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_writev>{
            using args_type = std::tuple<const struct iovec*, unsigned, __u64>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_write_fixed>{
            using args_type = std::tuple<const struct iovec*, unsigned, __u64, int>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_sendfile>{
            using args_type = std::tuple<int, int64_t, unsigned int, unsigned int>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_read_fixed>{
            using args_type = std::tuple<const struct iovec*, unsigned, __u64, int>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_readv>{
            using args_type = std::tuple<const struct iovec*, unsigned, __u64>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_recvmsg>{
            using args_type = std::tuple<msghdr*, int>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_recv>{
            using args_type = std::tuple<void*, size_t, int>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_read>{
            using args_type = std::tuple<void*, unsigned int, __u64>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_connect>{
            using args_type = std::tuple<const struct sockaddr*, socklen_t>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_fsync>{
            using args_type = std::tuple<unsigned int>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_fallocate>{
            using args_type = std::tuple<int, __u64, __u64>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_fadvise>{
            using args_type = std::tuple<__u64, off_t, int>;
        };

        // fs
        template<>
        struct args_traits<wrap_io_uring_prep_openat>{
            using args_type = std::tuple<int, const char*, int, mode_t>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_renameat>{
            using args_type = std::tuple<int, const char*, int, const char*, unsigned int>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_unlinkat>{
            using args_type = std::tuple<int, const char*, int>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_mkdirat>{
            using args_type = std::tuple<int, const char*, mode_t>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_statx>{
            using args_type = std::tuple<int, const char*, int, unsigned int, struct statx*>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_linkat>{
            using args_type = std::tuple<int, const char*, int, const char*, int>;
        };
        template<>
        struct args_traits<wrap_io_uring_prep_symlinkat>{
            using args_type = std::tuple<const char*, int, const char*>;
        };

        template<auto func>
        struct is_poll_add{
            static constexpr bool value = false;
        };

        template<>
        struct is_poll_add<wrap_io_uring_prep_poll_add>{
            static constexpr bool value = true;
        };

        template<typename T>
        concept has_custom_awaiter = requires(T&& t) {
            t.operator co_await();
        };
    };

    namespace details{
        template<typename T>
        class io_result{
        private:
            T value;
        public:
            int err = -1;
            io_result() noexcept: value(), err(-1){}
            io_result(const io_result& other) noexcept: value(other.value), err(other.err){}
            io_result(io_result&& other) noexcept: value(std::move(other.value)), err(other.err){}
            void operator=(io_result&& other) noexcept{
                value = std::move(other.value);
                err = other.err;
            }
            template<class V, class E>
            io_result(V&& v, E&& err) noexcept: value(std::forward<V>(v)), err(std::forward<E>(err)){}
            ~io_result(){}

            T& operator*() noexcept{
                return value;
            }
            const T& operator*() const noexcept{
                return value;
            }
            T* operator->() noexcept{
                return &value;
            }
            const T* operator->() const noexcept{
                return &value;
            }

            template<class Other, typename std::enable_if<!std::is_same<Other, bool>::value, bool>::type>
            operator Other() = delete;

            T&& move() noexcept{
                return std::move(value);
            }

            int error() const noexcept{ return err; }
            operator bool() const noexcept { return err == 0; }
            bool cancelled() const noexcept { return this->err == ECANCELED; }
            bool wouldblock() const noexcept { return this->err == EWOULDBLOCK; }
            bool again() const noexcept { return this->err == EAGAIN; }
            bool timed() const noexcept { return this->err == ETIME; }
            bool rst() const noexcept { return this->err == ECONNRESET; }

            void destroy() noexcept{
                value.~T();
            }
        };

        template<>
        class io_result<void>{
        public:
            int err;

            io_result() noexcept: err(SOCKET_ERROR){}
            io_result(const io_result&) = default;
            io_result(io_result&& other) noexcept: err(other.err){}
            void operator=(io_result&& other) noexcept{
                err = other.err;
            }
            io_result(int err) noexcept: err(err){
            }
            ~io_result() = default;

            int error() const noexcept{ return err; }
            operator bool() const noexcept { return err == 0; }
            bool cancelled() const noexcept { return this->err == ECANCELED; }
            bool wouldblock() const noexcept { return this->err == EWOULDBLOCK; }
            bool again() const noexcept { return this->err == EAGAIN; }
            bool timed() const noexcept { return this->err == ETIME; }
            bool rst() const noexcept { return this->err == ECONNRESET; }
        };

        template<class Awaiter, class Promise>
        struct Wrapper {
            Awaiter a;

            bool await_ready() noexcept(noexcept(a.await_ready())){
                return a.await_ready();
            }
            
            auto await_suspend(std::coroutine_handle<Promise> h) noexcept(noexcept(a.await_suspend(h))){
                return a.await_suspend(h);
            }

            auto await_resume() noexcept(noexcept(a.await_resume())){
                if constexpr(requires{ a.handle(); }){
                    if(a.handle() != nullptr){
                        auto h = std::coroutine_handle<Promise>::from_address(a.handle().address());
                        h.promise().xcoro_unhook();
                    }
                }
                return a.await_resume();
            }
        };

        struct CancelHandle{
            template<class T>
            static details::io_result<bool> call_traits(void* p) noexcept{
                return (*static_cast<T*>(p)).cancel();
            }

            typedef details::io_result<bool>(*Call)(void*);

            Call call_func;
            void* p;
        public:
            CancelHandle() = default;
            ~CancelHandle() = default;
            CancelHandle(const CancelHandle&) = default;
            CancelHandle& operator=(const CancelHandle&) = default;
            template<class T>
            CancelHandle(T* t) noexcept: call_func(call_traits<T>), p(t)
            {}
            
            details::io_result<bool> cancel() noexcept { return this->call_func(this->p); }
            bool invalid() const noexcept { return this->p == nullptr; }
            void reset() noexcept { this->p = nullptr; }
        };

#ifndef XNET_DISABLE_THREAD_SAFE
        struct alignas(64) CancelChain{
            CancelHandle cancelhandle;
            std::atomic<int> use_count = 1;
            std::atomic_flag spinlock = ATOMIC_FLAG_INIT;

            void lock() noexcept {
                while(spinlock.test_and_set(std::memory_order_acquire)){
                    XNET_CPU_RELAX();
                }
            }
            void unlock() noexcept{
                spinlock.clear(std::memory_order_release);
            }

            int add_ref() noexcept{
                return this->use_count.fetch_add(1, std::memory_order_acq_rel) + 1;
            }
            int dec_ref() noexcept{
                return this->use_count.fetch_sub(1, std::memory_order_acq_rel) - 1;
            }
        };

        struct alignas(64) shared_cancel_chain{
            std::atomic<shared_cancel_chain*> parent = nullptr;
            std::atomic<CancelChain*> chain = nullptr;
            std::atomic<int> state = 0b00;
            std::atomic<int> use_count = 1;
        };

        template<class T, class Task>
        struct cancelable_promise_type{
            using promise_type = cancelable_promise_type<T, Task>;
            shared_cancel_chain data;

            std::coroutine_handle<> waiter = nullptr;

            T value{};

            ~cancelable_promise_type(){
                CancelChain* chain = this->data.chain.load(std::memory_order_acquire);
                if(chain && chain->dec_ref() == 0){
                    delete chain;
                }
            }

            bool xcoro_hook_recursion(shared_cancel_chain* node) noexcept{
                int expected = 0b00;
                if(!node->state.compare_exchange_strong(expected, 0b10, std::memory_order_release, std::memory_order_relaxed)){
                    return false;
                }
                shared_cancel_chain* next = node->parent.load(std::memory_order_acquire);
                if(next){
                    if(!xcoro_hook_recursion(next)){
                        node->state.fetch_and(0b01, std::memory_order_relaxed);
                        return false;
                    }
                }
                return true;
            }

            template<class IO>
            bool xcoro_hook(IO* io) noexcept{
                CancelChain* chain = this->data.chain.load(std::memory_order_acquire);
                if(!chain){
                    // root
                    this->data.chain.store(new CancelChain{CancelHandle(io), 1}, std::memory_order_release);
                    int expected = 0b00;
                    if(!this->data.state.compare_exchange_strong(expected, 0b10, std::memory_order_release)){
                        return false;
                    }
                    return true;
                }
                else{
                    // other
                    shared_cancel_chain* node = this->data.parent.load(std::memory_order_acquire);
                    chain->lock();
                    chain->cancelhandle = CancelHandle(io);
                    int expected = 0b00;
                    if(!this->data.state.compare_exchange_strong(expected, 0b10, std::memory_order_release)){
                        chain->unlock();
                        return false;
                    }
                    if(node == nullptr){
                        chain->unlock();
                        return true;
                    }
                    if(!xcoro_hook_recursion(node)){
                        this->data.state.fetch_and(0b01, std::memory_order_release);
                        chain->unlock();
                        return false;
                    }

                    chain->unlock();
                    return true;
                }
            }

            void xcoro_unhook() noexcept{
                shared_cancel_chain* node = &this->data;
                while(node){
                    node->state.fetch_and(0b01, std::memory_order_release);
                    node = node->parent.load(std::memory_order_acquire);
                }
            }

            details::io_result<bool> request_cancel() noexcept{
                int expected = this->data.state.load(std::memory_order_acquire);
                while(true){
                    if(expected & 0b01){
                        return {true, 0};
                    }

                    int desired = expected | 0b01;
                    if(this->data.state.compare_exchange_weak(expected, desired, std::memory_order_acq_rel)){
                        if(expected == 0b10){
                            CancelChain* chain = this->data.chain.load(std::memory_order_acquire);
                            chain->lock();
                            // check one more time if hook failed or unhook
                            if(this->data.state.load(std::memory_order_acquire) & 0b10){
                                details::io_result<bool> ret = chain->cancelhandle.cancel();
                                chain->unlock();
                                return ret;
                            }
                            chain->unlock();
                        }
                        return {true, 0};
                    }
                }
                return {true, 0};
            }

            int add_ref() noexcept{
                return this->data.use_count.fetch_add(1, std::memory_order_acq_rel) + 1;
            }

            int dec_ref() noexcept{
                return this->data.use_count.fetch_sub(1, std::memory_order_acq_rel) - 1;
            }

            template<typename Awaiter>
                requires (details::has_custom_awaiter<Awaiter>)
            auto await_transform(Awaiter&& awaiter) noexcept{
                using awaiter_t = std::decay_t<decltype(awaiter.operator co_await())>;
                return details::Wrapper<awaiter_t, promise_type>{std::forward<Awaiter>(awaiter).operator co_await()};
            }

            template<typename Awaiter>
                requires (!details::has_custom_awaiter<Awaiter>)
            auto await_transform(Awaiter&& awaiter) noexcept{
                return details::Wrapper<Awaiter, promise_type>{std::forward<Awaiter>(awaiter)};
            }

            Task get_return_object() noexcept {
                return Task{
                    std::coroutine_handle<promise_type>::from_promise(*this)
                };
            }

            static Task get_return_object_on_allocation_failure() noexcept{
                return Task{nullptr};
            }

            std::suspend_always initial_suspend() noexcept { return {}; }

            auto final_suspend() noexcept{
                struct awaiter{
                    bool await_ready() const noexcept { return false; }
                    std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept{
                        auto w = h.promise().waiter;
                        return w ? w : std::noop_coroutine();
                    }

                    void await_resume() const noexcept {}
                };
                return awaiter{};
            }

            template<class V>
            void return_value(V&& v) noexcept{
                this->value = T(std::forward<V>(v));
            }

            void return_value(T&& v) noexcept {
                value = std::move(v);
            }

            void unhandled_exception() noexcept {}
        };

        template<class Task>
        struct cancelable_promise_type<void, Task>{
            using promise_type = cancelable_promise_type<void, Task>;
            shared_cancel_chain data;

            std::coroutine_handle<> waiter = nullptr;

            ~cancelable_promise_type(){
                CancelChain* chain = this->data.chain.load(std::memory_order_acquire);
                if(chain && chain->dec_ref() == 0){
                    delete chain;
                }
            }

            bool xcoro_hook_recursion(shared_cancel_chain* node) noexcept{
                int expected = 0b00;
                if(!node->state.compare_exchange_strong(expected, 0b10, std::memory_order_release, std::memory_order_relaxed)){
                    return false;
                }
                shared_cancel_chain* next = node->parent.load(std::memory_order_acquire);
                if(next){
                    if(!xcoro_hook_recursion(next)){
                        node->state.fetch_and(0b01, std::memory_order_relaxed);
                        return false;
                    }
                }
                return true;
            }

            template<class IO>
            bool xcoro_hook(IO* io) noexcept{
                CancelChain* chain = this->data.chain.load(std::memory_order_acquire);
                if(!chain){
                    // root
                    this->data.chain.store(new CancelChain{CancelHandle(io), 1}, std::memory_order_release);
                    int expected = 0b00;
                    if(!this->data.state.compare_exchange_strong(expected, 0b10, std::memory_order_release)){
                        return false;
                    }
                    return true;
                }
                else{
                    // other
                    shared_cancel_chain* node = this->data.parent.load(std::memory_order_acquire);
                    chain->lock();
                    chain->cancelhandle = CancelHandle(io);
                    int expected = 0b00;
                    if(!this->data.state.compare_exchange_strong(expected, 0b10, std::memory_order_release)){
                        chain->unlock();
                        return false;
                    }
                    if(node == nullptr){
                        chain->unlock();
                        return true;
                    }
                    if(!xcoro_hook_recursion(node)){
                        this->data.state.fetch_and(0b01, std::memory_order_release);
                        chain->unlock();
                        return false;
                    }

                    chain->unlock();
                    return true;
                }
            }

            void xcoro_unhook() noexcept{
                shared_cancel_chain* node = &this->data;
                while(node){
                    node->state.fetch_and(0b01, std::memory_order_release);
                    node = node->parent.load(std::memory_order_acquire);
                }
            }

            details::io_result<bool> request_cancel() noexcept{
                int expected = this->data.state.load(std::memory_order_acquire);
                while(true){
                    if(expected & 0b01){
                        return {true, 0};
                    }

                    int desired = expected | 0b01;
                    if(this->data.state.compare_exchange_weak(expected, desired, std::memory_order_acq_rel)){
                        if(expected == 0b10){
                            CancelChain* chain = this->data.chain.load(std::memory_order_acquire);
                            chain->lock();
                            // check one more time if hook failed or unhook
                            if(this->data.state.load(std::memory_order_acquire) & 0b10){
                                details::io_result<bool> ret = chain->cancelhandle.cancel();
                                chain->unlock();
                                return ret;
                            }
                            chain->unlock();
                        }
                        return {true, 0};
                    }
                }
                return {true, 0};
            }

            int add_ref() noexcept{
                return this->data.use_count.fetch_add(1, std::memory_order_acq_rel) + 1;
            }

            int dec_ref() noexcept{
                return this->data.use_count.fetch_sub(1, std::memory_order_acq_rel) - 1;
            }

            template<typename Awaiter>
                requires (details::has_custom_awaiter<Awaiter>)
            auto await_transform(Awaiter&& awaiter) noexcept{
                using awaiter_t = std::decay_t<decltype(awaiter.operator co_await())>;
                return details::Wrapper<awaiter_t, promise_type>{std::forward<Awaiter>(awaiter).operator co_await()};
            }

            template<typename Awaiter>
                requires (!details::has_custom_awaiter<Awaiter>)
            auto await_transform(Awaiter&& awaiter) noexcept{
                return details::Wrapper<Awaiter, promise_type>{std::forward<Awaiter>(awaiter)};
            }

            Task get_return_object() noexcept {
                return Task{
                    std::coroutine_handle<promise_type>::from_promise(*this)
                };
            }

            static Task get_return_object_on_allocation_failure() noexcept{
                return Task{nullptr};
            }

            std::suspend_always initial_suspend() noexcept { return {}; }

            auto final_suspend() noexcept{
                struct awaiter{
                    bool await_ready() const noexcept { return false; }
                    std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept{
                        auto w = h.promise().waiter;
                        return w ? w : std::noop_coroutine();
                    }

                    void await_resume() const noexcept {}
                };
                return awaiter{};
            }

            void return_void() noexcept {}

            void unhandled_exception() noexcept {}
        };
#else
        struct CancelChain{
            CancelHandle cancelhandle;
            int use_count = 1;
        
            int add_ref() noexcept{ return ++use_count; }
            int dec_ref() noexcept{ return --use_count; }
        };

        struct shared_cancel_chain{
            shared_cancel_chain* parent = nullptr;
            CancelChain* chain = nullptr;
            int state = 0b00;
            int use_count = 1;
        };

        template<class T, class Task>
        struct cancelable_promise_type{
            using promise_type = cancelable_promise_type<T, Task>;
            shared_cancel_chain data;

            std::coroutine_handle<> waiter = nullptr;

            T value{};

            ~cancelable_promise_type(){
                CancelChain* chain = this->data.chain;
                if(chain && chain->dec_ref() == 0){
                    delete chain;
                }
            }

            bool xcoro_hook_recursion(shared_cancel_chain* node) noexcept{
                if(node->state != 0b00){
                    return false;
                }
                node->state = 0b10;
                shared_cancel_chain* next = node->parent;
                if(next){
                    if(!xcoro_hook_recursion(next)){
                        node->state &= 0b01;
                        return false;
                    }
                }
                return true;
            }

            template<class IO>
            bool xcoro_hook(IO* io) noexcept{
                CancelChain* chain = this->data.chain;
                if(!chain){
                    // root
                    if(this->data.state != 0b00){
                        return false;
                    }
                    this->data.chain = new CancelChain{CancelHandle(io), 1};
                    this->data.state = 0b10;
                    return true; 
                }
                else{
                    shared_cancel_chain* node = this->data.parent;
                    chain->cancelhandle = CancelHandle(io);
                    if(this->data.state != 0b00){
                        return false;
                    }
                    this->data.state = 0b10;
                    if(!node){
                        return true;
                    }
                    if(!xcoro_hook_recursion(node)){
                        this->data.state &= 0b01;
                        return false;
                    }
                    return true;
                }
            }

            void xcoro_unhook() noexcept{
                shared_cancel_chain* node = &this->data;
                while(node){
                    node->state &= 0b01;
                    node = node->parent;
                }
            }

            details::io_result<bool> request_cancel() noexcept{
                int expected = this->data.state;
                this->data.state &= 0b01;
                if(expected == 0b10){
                    return this->data.chain->cancelhandle.cancel();
                }
                return {true, 0};
            }

            int add_ref() noexcept{ return ++this->data.use_count; }
            int dec_ref() noexcept{ return --this->data.use_count; }

            template<typename Awaiter>
                requires (details::has_custom_awaiter<Awaiter>)
            auto await_transform(Awaiter&& awaiter) noexcept{
                using awaiter_t = std::decay_t<decltype(awaiter.operator co_await())>;
                return details::Wrapper<awaiter_t, promise_type>{std::forward<Awaiter>(awaiter).operator co_await()};
            }

            template<typename Awaiter>
                requires (!details::has_custom_awaiter<Awaiter>)
            auto await_transform(Awaiter&& awaiter) noexcept{
                return details::Wrapper<Awaiter, promise_type>{std::forward<Awaiter>(awaiter)};
            }

            Task get_return_object() noexcept {
                return Task{
                    std::coroutine_handle<promise_type>::from_promise(*this)
                };
            }

            static Task get_return_object_on_allocation_failure() noexcept{
                return Task{nullptr};
            }

            std::suspend_always initial_suspend() noexcept { return {}; }

            auto final_suspend() noexcept{
                struct awaiter{
                    bool await_ready() const noexcept { return false; }
                    std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept{
                        auto w = h.promise().waiter;
                        return w ? w : std::noop_coroutine();
                    }

                    void await_resume() const noexcept {}
                };
                return awaiter{};
            }

            template<class V>
            void return_value(V&& v) noexcept{
                this->value = T(std::forward<V>(v));
            }

            void return_value(T&& v) noexcept {
                value = std::move(v);
            }

            void unhandled_exception() noexcept {}
        };

        template<class Task>
        struct cancelable_promise_type<void, Task>{
            using promise_type = cancelable_promise_type<void, Task>;
            shared_cancel_chain data;

            std::coroutine_handle<> waiter = nullptr;

            ~cancelable_promise_type(){
                CancelChain* chain = this->data.chain;
                if(chain && chain->dec_ref() == 0){
                    delete chain;
                }
            }

            bool xcoro_hook_recursion(shared_cancel_chain* node) noexcept{
                if(node->state != 0b00){
                    return false;
                }
                node->state = 0b10;
                shared_cancel_chain* next = node->parent;
                if(next){
                    if(!xcoro_hook_recursion(next)){
                        node->state &= 0b01;
                        return false;
                    }
                }
                return true;
            }

            template<class IO>
            bool xcoro_hook(IO* io) noexcept{
                CancelChain* chain = this->data.chain;
                if(!chain){
                    // root
                    if(this->data.state != 0b00){
                        return false;
                    }
                    this->data.chain = new CancelChain{CancelHandle(io), 1};
                    this->data.state = 0b10;
                    return true; 
                }
                else{
                    shared_cancel_chain* node = this->data.parent;
                    chain->cancelhandle = CancelHandle(io);
                    if(this->data.state != 0b00){
                        return false;
                    }
                    this->data.state = 0b10;
                    if(!node){
                        return true;
                    }
                    if(!xcoro_hook_recursion(node)){
                        this->data.state &= 0b01;
                        return false;
                    }
                    return true;
                }
            }

            void xcoro_unhook() noexcept{
                shared_cancel_chain* node = &this->data;
                while(node){
                    node->state &= 0b01;
                    node = node->parent;
                }
            }

            details::io_result<bool> request_cancel() noexcept{
                int expected = this->data.state;
                this->data.state &= 0b01;
                if(expected == 0b10){
                    return this->data.chain->cancelhandle.cancel();
                }
                return {true, 0};
            }

            int add_ref() noexcept{ return ++this->data.use_count; }
            int dec_ref() noexcept{ return --this->data.use_count; }

            template<typename Awaiter>
                requires (details::has_custom_awaiter<Awaiter>)
            auto await_transform(Awaiter&& awaiter) noexcept{
                using awaiter_t = std::decay_t<decltype(awaiter.operator co_await())>;
                return details::Wrapper<awaiter_t, promise_type>{std::forward<Awaiter>(awaiter).operator co_await()};
            }

            template<typename Awaiter>
                requires (!details::has_custom_awaiter<Awaiter>)
            auto await_transform(Awaiter&& awaiter) noexcept{
                return details::Wrapper<Awaiter, promise_type>{std::forward<Awaiter>(awaiter)};
            }

            Task get_return_object() noexcept {
                return Task{
                    std::coroutine_handle<promise_type>::from_promise(*this)
                };
            }

            static Task get_return_object_on_allocation_failure() noexcept{
                return Task{nullptr};
            }

            std::suspend_always initial_suspend() noexcept { return {}; }

            auto final_suspend() noexcept{
                struct awaiter{
                    bool await_ready() const noexcept { return false; }
                    std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept{
                        auto w = h.promise().waiter;
                        return w ? w : std::noop_coroutine();
                    }

                    void await_resume() const noexcept {}
                };
                return awaiter{};
            }

            void return_void() noexcept {}

            void unhandled_exception() noexcept {}
        };
#endif

        template<class T>
        struct is_cancelable_promise{
            static constexpr bool value = false;
        };
        template<class Ret, class Task>
        struct is_cancelable_promise<cancelable_promise_type<Ret, Task>>{
            static constexpr bool value = true;
        };
    };

    using CancelHandle = details::CancelHandle;

    template<typename T = void>
    struct task{
        using promise_type = details::cancelable_promise_type<T, task<T>>;
        std::coroutine_handle<promise_type> h = nullptr;

        task(std::coroutine_handle<promise_type> h) noexcept : h(h) {}
        task(task&& other) noexcept : h(std::exchange(other.h, nullptr)) {}
        void operator=(task&& other) noexcept{
            if(this->h){
                if(this->h.promise().dec_ref() == 0){
                    this->h.destroy();
                }
            }
            this->h = std::exchange(other.h, nullptr);
        }
        task(const task&) = delete;

        ~task(){
            if(this->h && this->h.promise().dec_ref() == 0){
                this->h.destroy();
            }
        }

        bool invalid() const noexcept { return this->h == nullptr; }

        details::io_result<bool> cancel() noexcept{
            return h.promise().request_cancel();
        }

        bool await_ready() const noexcept{
            return !h || h.done();
        }

        template<class Promise>
        auto await_suspend(std::coroutine_handle<Promise> caller) noexcept{
            auto& promise = h.promise();
    #ifndef XNET_DISABLE_THREAD_SAFE
            if constexpr(details::is_cancelable_promise<Promise>::value){
                details::CancelChain* chain = caller.promise().data.chain.load(std::memory_order_acquire);
                if(!chain){
                    chain = new details::CancelChain{CancelHandle(), 2};
                    caller.promise().data.chain.store(chain, std::memory_order_relaxed);
                    promise.data.chain.store(chain, std::memory_order_relaxed);
                }
                else{
                    chain->use_count.fetch_add(1, std::memory_order_relaxed);
                    promise.data.chain.store(chain, std::memory_order_relaxed);
                }
                promise.data.parent.store(&caller.promise().data, std::memory_order_release);
            }
    #else
            if constexpr(details::is_cancelable_promise<Promise>::value){
                details::CancelChain* chain = caller.promise().data.chain;
                if(!chain){
                    promise.data.chain = caller.promise().data.chain = new details::CancelChain{CancelHandle(), 2};
                }
                else{
                    chain->use_count += 1;
                    promise.data.chain = chain;
                }
                promise.data.parent = &caller.promise().data;
            }
    #endif
            promise.waiter = caller;
            return h;
        }

        template<class Ret, typename std::enable_if<!std::is_same<Ret, void>::value, bool>::type = true>
        Ret get_resume_value() noexcept{
            return std::move(h.promise().value);
        }
        template<class Ret, typename std::enable_if<std::is_same<Ret, void>::value, bool>::type = true>
        Ret get_resume_value() noexcept{
            return;
        }

        T await_resume() noexcept{
            return get_resume_value<T>();
        }

        struct CancelToken{
            std::coroutine_handle<promise_type> h;

            CancelToken() = delete;
            CancelToken(std::coroutine_handle<promise_type> h) noexcept : h(h) {
                this->h.promise().add_ref();
            }
            CancelToken(const CancelToken& other) noexcept: h(other.h){
                h.promise().add_ref();
            }
            CancelToken& operator=(const CancelToken& other) noexcept{
                if(this->h != other.h){
                    if(this->h && this->h.promise().dec_ref() == 0){
                        this->h.destroy();
                    }
                    this->h = other.h;
                    other.h.promise().add_ref();
                }
                return *this;
            }
            CancelToken(CancelToken&& other) noexcept: h(std::exchange(other.h, nullptr)){
            }
            CancelToken& operator=(CancelToken&& other) noexcept{
                if(this->h && this->h.promise().dec_ref() == 0){
                    this->h.destroy();
                }
                this->h = std::exchange(other.h, nullptr);
                return *this;
            }
            ~CancelToken(){
                if(this->h && this->h.promise().dec_ref() == 0){
                    this->h.destroy();
                }
            }

            details::io_result<bool> cancel() noexcept{
                return h.promise().request_cancel();
            }
        };

        CancelToken cancel_token() noexcept{
            return CancelToken(this->h);
        }
    };

    struct detached_task {
        struct promise_type {
            detached_task get_return_object() noexcept { return {}; }
            static detached_task get_return_object_on_allocation_failure() noexcept { return {}; }

            std::suspend_never initial_suspend() noexcept { return {}; }
            std::suspend_never final_suspend() noexcept { return {}; }

            void return_void() noexcept {}
            void unhandled_exception() noexcept {}
        };
    };

    template<typename T = void>
    struct ptask {
        struct promise_type {
            std::coroutine_handle<> waiter = nullptr;
            T value{};

            ptask<T> get_return_object() noexcept {
                return ptask<T>{
                    std::coroutine_handle<promise_type>::from_promise(*this)
                };
            }

            static ptask<T> get_return_object_on_allocation_failure() noexcept{
                return ptask<T>{nullptr};
            }

            std::suspend_always initial_suspend() noexcept { return {}; }

            auto final_suspend() noexcept{
                struct awaiter{
                    bool await_ready() const noexcept { return false; }
                    std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept{
                        auto w = h.promise().waiter;
                        return w ? w : std::noop_coroutine();
                    }

                    void await_resume() const noexcept {}
                };
                return awaiter{};
            }

            template<class V>
            void return_value(V&& v) noexcept{
                value = std::forward<V>(v);
            }

            void return_value(T&& v) noexcept {
                value = std::move(v);
            }

            void unhandled_exception() noexcept {}
        };

        std::coroutine_handle<promise_type> h = nullptr;

        ptask(std::coroutine_handle<promise_type> h) noexcept : h(h) {}
        ptask(ptask&& other) noexcept : h(std::exchange(other.h, nullptr)) {}
        void operator=(ptask&& other) noexcept{
            if(this->h) this->h.destroy();
            this->h = std::exchange(other.h, nullptr);
        }
        ptask(const ptask&) = delete;

        ~ptask() {
            if (h){
                h.destroy();
            }
        }

        void start() & noexcept { 
            h.resume();
        }
        void start() && noexcept = delete;

        bool invalid() const noexcept { return this->h == nullptr; }

        bool await_ready() const noexcept {
            return !h || h.done();
        }

        auto await_suspend(std::coroutine_handle<> caller) noexcept {
            h.promise().waiter = caller;
            return h;
        }

        T await_resume() noexcept {
            return std::move(h.promise().value);
        }
    };

    template<>
    struct ptask<void> {
        struct promise_type{
            std::coroutine_handle<> waiter = nullptr;

            ptask<void> get_return_object() noexcept {
                return ptask<void>{
                    std::coroutine_handle<promise_type>::from_promise(*this)
                };
            }

            static ptask<void> get_return_object_on_allocation_failure() noexcept{
                return ptask<void>{nullptr};
            }

            std::suspend_always initial_suspend() noexcept { return {}; }

            auto final_suspend() noexcept{
                struct awaiter{
                    bool await_ready() const noexcept { return false; }
                    std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept{
                        auto w = h.promise().waiter;
                        return w ? w : std::noop_coroutine();
                    }

                    void await_resume() const noexcept {}
                };
                return awaiter{};
            }

            void return_void() noexcept {}

            void unhandled_exception() noexcept {}
        };

        std::coroutine_handle<promise_type> h;

        ptask(std::coroutine_handle<promise_type> h) noexcept : h(h) {}
        ptask(ptask&& other) noexcept : h(std::exchange(other.h, nullptr)) {}
        void operator=(ptask&& other) noexcept{
            if(this->h) this->h.destroy();
            this->h = std::exchange(other.h, nullptr);
        }
        ptask(const ptask&) = delete;

        ~ptask() {
            if (h){
                h.destroy();
            }
        }

        void start() & noexcept { 
            h.resume();
        }
        void start() && noexcept = delete;

        bool invalid() const noexcept { return this->h == nullptr; }

        bool await_ready() const noexcept {
            return !h || h.done();
        }

        auto await_suspend(std::coroutine_handle<> caller) noexcept {
            h.promise().waiter = caller;
            return h;
        }

        void await_resume() noexcept {}
    };

    namespace details{
        template<bool multi_threaded, bool fastfail, class... Ops>
        class [[nodiscard]] AllAwaiter{
            template<size_t hit, size_t i>
            void cancel_except() noexcept{
                if constexpr(i < num_ops){
                    if constexpr(i != hit && requires{std::get<i>(ops).cancel();}){
                        (void)std::get<i>(ops).cancel();
                    }
                    return cancel_except<hit, i+1>();
                }
            }

            template<size_t... Is>
            void start_all_tasks(std::index_sequence<Is...>) noexcept{
                (worker<Is>(*this, std::get<Is>(this->ops)), ...);
            }

            template<size_t worker_id, class Op>
            static xnet::detached_task worker(AllAwaiter& awaiter, Op& op) noexcept{
                auto result = co_await op;
                if constexpr(fastfail){
                    // all
                    // negative represents failure

                    static_assert(requires{result.error();}, 
                    "AllAwaiter<false, ...>::worker(): co_await result should have error() interface when enabling fastfail"); 

                    bool ok = !result.error();
                    bool need_cancel = false;
                    bool need_resume = false;

                    std::get<worker_id>(awaiter.result) = std::move(result);
                    if(awaiter.done < 0){
                        --awaiter.done;
                    }
                    else{
                        if(ok){
                            ++awaiter.done;
                        }
                        else{
                            // frist failure
                            awaiter.done = -(awaiter.done + 1);
                            need_cancel = true;
                        }
                    }
                    need_resume = (awaiter.done >= 0 ? awaiter.done : -awaiter.done) == num_ops;

                    if(need_cancel){
                        awaiter.cancel_except<worker_id, 0>(); 
                    }
                    if(need_resume){
                        awaiter.handler.resume();
                    }
                }
                else{
                    // allSettled
                    ++awaiter.done;
                    std::get<worker_id>(awaiter.result) = std::move(result);
                    if(awaiter.done == num_ops){
                        awaiter.handler.resume();
                    }
                }
            }
            std::tuple<Ops...> ops;
            std::tuple<await_result_t<Ops>...> result;
            std::coroutine_handle<> handler;
            int done;
        public:
            static constexpr size_t num_ops = sizeof...(Ops);

            AllAwaiter(Ops&&... ops) noexcept: ops(std::forward<Ops>(ops)...), result({}), handler(nullptr), done(0)
            {}
            AllAwaiter(AllAwaiter&&) = default;
            ~AllAwaiter() = default;

            details::io_result<bool> cancel() noexcept{
                if(this->handler == nullptr){
                    return {false, ECANCELED};
                }
                this->cancel_except<num_ops + 1, 0>();
                return {true, 0};
            }
            std::coroutine_handle<>& handle() noexcept{ return this->handler; }

            bool await_ready() const noexcept { return false; }

            template<class Promise>
            bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
                this->handler = handle;
                if constexpr(requires{ handle.promise().xcoro_hook(this); }){
                    if(!handle.promise().xcoro_hook(this)){
                        return false;
                    }
                }
                this->start_all_tasks(std::make_index_sequence<num_ops>());
                return true;
            }

            std::tuple<await_result_t<Ops>...>&& await_resume() noexcept {
                this->handler = nullptr;
                return std::move(this->result);
            }
        };


        template<bool multi_threaded, bool fastfail, class... Ops>
        class [[nodiscard]] AnyAwaiter{
            template<class... Ts>
            class Result{
                friend class AnyAwaiter;
                template<size_t... vs>
                static consteval size_t get_max_value(){
                    size_t max = 0;
                    for(size_t v : {vs...}){
                        max = v > max ? v : max;
                    }
                    return max;
                }

                static constexpr size_t max_size = get_max_value<sizeof(Ts)...>();
                static constexpr size_t max_align = get_max_value<alignof(Ts)...>();

                int idx = -1;
                alignas(max_align) unsigned char bytes[max_size];

                template<size_t I>
                void try_destroy() noexcept{
                    if(I == this->idx){
                        auto& ret = this->get<I>();
                        using T = typename std::decay<decltype(ret)>::type;
                        ret.~T();
                        return;
                    }
                    else{
                        if constexpr(I != (sizeof...(Ts) - 1)){
                            return try_destroy<I+1>();
                        }
                    }
                }
            public:
                Result() = default;
                Result(const Result&) = delete;
                Result(Result&& other) noexcept: idx(std::exchange(other.idx, -1)){
                    if(this->idx >= 0){
                        std::memcpy(this->bytes, other.bytes, max_size); 
                    }
                }
                Result& operator=(Result&& other) noexcept{
                    if(this->idx >= 0){
                        this->try_destroy<0>();
                    }
                    this->idx = std::exchange(other.idx, -1);
                    if(this->idx >= 0){
                        std::memcpy(this->bytes, other.bytes, max_size); 
                    }
                    return *this;
                }
                ~Result(){
                    if(this->idx >= 0){
                        this->try_destroy<0>();
                    }
                }
                int who() const noexcept { return this->idx; }
                
                template<size_t I>
                struct type{
                    using nth_type = typename std::tuple_element<I, std::tuple<Ts...>>::type;
                };
                template<size_t I>
                using type_t = typename type<I>::nth_type;

                template<size_t I>
                type_t<I>& get() noexcept{
                    return reinterpret_cast<type_t<I>&>(bytes);
                }
                template<size_t I>
                const type_t<I>& get() const noexcept{
                    return reinterpret_cast<const type_t<I>&>(bytes);
                }
            };

            template<size_t hit, size_t i>
            void cancel_except() noexcept{
                if constexpr(i < num_ops){
                    if constexpr(i != hit && requires{std::get<i>(ops).cancel();}){
                        (void)std::get<i>(ops).cancel();
                    }
                    return cancel_except<hit, i+1>();
                }
            }

            template<size_t... Is>
            void start_all_tasks(std::index_sequence<Is...>) noexcept{
                (worker<Is>(*this, std::get<Is>(this->ops)), ...);
            }

            template<size_t worker_id, class Op>
            static xnet::detached_task worker(AnyAwaiter& awaiter, Op& op) noexcept{
                auto result = co_await op;
                if constexpr(fastfail){
                    // race
                    bool need_cancel = false;
                    bool need_resume = false;

                    ++awaiter.done;
                    if(awaiter.done == 1){
                        // first
                        awaiter.result.idx = worker_id;
                        awaiter.result.template get<worker_id>() = std::move(result);
                        need_cancel = true;
                    }
                    need_resume = (awaiter.done == num_ops);

                    if(need_cancel){
                        awaiter.cancel_except<worker_id, 0>();
                    }
                    if(need_resume){
                        awaiter.handler.resume();
                    }
                }
                else{
                    // any
                    // postive represents success
                    static_assert(requires{result.error();}, 
                    "AnyAwaiter<false, ...>::worker(): co_await result should have error() interface when disabling fastfail");

                    bool ok = !result.error();
                    bool need_cancel = false;
                    bool need_resume = false;

                    if(awaiter.done > 0){
                        ++awaiter.done;
                    }
                    else{
                        if(ok){
                            // first success
                            awaiter.done = 1 - awaiter.done;
                            awaiter.result.idx = worker_id;
                            awaiter.result.template get<worker_id>() = std::move(result);
                            need_cancel = true;
                        }
                        else{
                            --awaiter.done;
                        }
                    }
                    need_resume = (awaiter.done >= 0 ? awaiter.done : -awaiter.done) == num_ops;

                    if(need_cancel){
                        awaiter.cancel_except<worker_id, 0>();
                    }
                    if(need_resume){
                        awaiter.handler.resume();
                    }
                }
            }

            std::tuple<Ops...> ops;
            Result<await_result_t<Ops>...> result;
            std::coroutine_handle<> handler;
            int idx;
            int done;
        public:
            static constexpr size_t num_ops = sizeof...(Ops);

            AnyAwaiter(Ops&&... ops) noexcept: ops(std::forward<Ops>(ops)...), result({}), handler(nullptr), done(0)
            {}
            AnyAwaiter(AnyAwaiter&&) = default;
            ~AnyAwaiter() = default;

            details::io_result<bool> cancel() noexcept{
                if(this->handler == nullptr){
                    return {false, ECANCELED};
                }
                this->cancel_except<num_ops + 1, 0>();
                return {true, 0};
            }
            std::coroutine_handle<>& handle() noexcept{ return this->handler; }

            bool await_ready() const noexcept { return false; }

            template<class Promise>
            bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
                this->handler = handle;
                if constexpr(requires{ handle.promise().xcoro_hook(this); }){
                    if(!handle.promise().xcoro_hook(this)){
                        return false;
                    }
                }
                this->start_all_tasks(std::make_index_sequence<num_ops>());
                return true;
            }

            auto&& await_resume() noexcept{
                this->handler = nullptr;
                return std::move(this->result);
            }
        };
    };

    namespace details{
        template<bool fastfail, class... Ops>
        class [[nodiscard]] AllAwaiter<true, fastfail, Ops...>{
            template<size_t hit, size_t i>
            void cancel_except() noexcept{
                if constexpr(i < num_ops){
                    if constexpr(i != hit && requires{std::get<i>(ops).cancel();}){
                        (void)std::get<i>(ops).cancel();
                    }
                    return cancel_except<hit, i+1>();
                }
            }

            template<size_t... Is>
            void start_all_tasks(std::index_sequence<Is...>) noexcept{
                (worker<Is>(*this, std::get<Is>(this->ops)), ...);
            }

            template<size_t worker_id, class Op>
            static xnet::detached_task worker(AllAwaiter& awaiter, Op& op) noexcept{
                auto result = co_await op;
                if constexpr(fastfail){
                    // all
                    // negative represents failure
                    static_assert(requires{result.error();}, 
                    "AllAwaiter<false, ...>::worker(): co_await result should have error() interface when enabling fastfail");

                    bool ok = !result.error();
                    bool need_cancel = false;
                    bool need_resume = false;
                    std::get<worker_id>(awaiter.result) = std::move(result);
                    int seen = awaiter.done.load(std::memory_order_acquire);

                    while(true){
                        int new_val;
                        if(ok){
                            new_val = (seen >= 0 ? seen + 1 : seen - 1);
                        }
                        else{
                            if(seen >= 0){
                                new_val = -(seen + 1);
                                need_cancel = true;
                            }
                            else{
                                new_val = seen - 1;
                            }
                        }
                        if(awaiter.done.compare_exchange_weak(seen, new_val, std::memory_order_acq_rel)){
                            int completed = (new_val >= 0 ? new_val : -new_val);
                            need_resume = completed == num_ops;
                            break;
                        }
                    }
                    
                    if(need_cancel){
                        // only cancel once
                        awaiter.cancel_except<worker_id, 0>(); 
                    }
                    if(need_resume){
                        awaiter.handler.resume();
                    }
                }
                else{
                    // allSettled
                    std::get<worker_id>(awaiter.result) = std::move(result);
                    int seen = awaiter.done.fetch_add(1, std::memory_order_acq_rel);
                    bool need_resume = (seen + 1 == num_ops);
                    if(need_resume){
                        awaiter.handler.resume();
                    }
                }
                co_return;
            }
            std::tuple<Ops...> ops;
            std::tuple<await_result_t<Ops>...> result;
            std::coroutine_handle<> handler;
            std::atomic<int> done;
        public:
            static constexpr size_t num_ops = sizeof...(Ops);

            AllAwaiter(Ops&&... ops) noexcept: ops(std::forward<Ops>(ops)...), result({}), handler(nullptr), done(0)
            {}
            AllAwaiter(AllAwaiter&& other) 
            noexcept: ops(std::move(other.ops)), result(std::move(other.result)), 
            handler(std::exchange(other.handler, nullptr)), done(other.done.load(std::memory_order_relaxed))
            {}
            ~AllAwaiter() = default;

            details::io_result<bool> cancel() noexcept{
                if(this->handler == nullptr){
                    return {false, ECANCELED};
                }
                this->cancel_except<num_ops + 1, 0>();
                return {true, 0};
            }
            std::coroutine_handle<>& handle() noexcept{ return this->handler; }

            bool await_ready() const noexcept { return false; }

            template<class Promise>
            bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
                this->handler = handle;
                if constexpr(requires{ handle.promise().xcoro_hook(this); }){
                    if(!handle.promise().xcoro_hook(this)){
                        return false;
                    }
                }
                this->start_all_tasks(std::make_index_sequence<num_ops>());
                return true;
            }

            std::tuple<await_result_t<Ops>...>&& await_resume() noexcept {
                this->handler = nullptr;
                return std::move(this->result);
            }
        };

        template<bool fastfail, class... Ops>
        class [[nodiscard]] AnyAwaiter<true, fastfail, Ops...>{
            template<class... Ts>
            class Result{
                friend class AnyAwaiter;
                template<size_t... vs>
                static consteval size_t get_max_value(){
                    size_t max = 0;
                    for(size_t v : {vs...}){
                        max = v > max ? v : max;
                    }
                    return max;
                }

                static constexpr size_t max_size = get_max_value<sizeof(Ts)...>();
                static constexpr size_t max_align = get_max_value<alignof(Ts)...>();

                int idx = -1;
                alignas(max_align) unsigned char bytes[max_size];

                template<size_t I>
                void try_destroy() noexcept{
                    if(I == this->idx){
                        auto& ret = this->get<I>();
                        using T = typename std::decay<decltype(ret)>::type;
                        ret.~T();
                        return;
                    }
                    else{
                        if constexpr(I != (sizeof...(Ts) - 1)){
                            return try_destroy<I+1>();
                        }
                    }
                }
            public:
                Result() = default;
                Result(const Result&) = delete;
                Result(Result&& other) noexcept: idx(std::exchange(other.idx, -1)){
                    if(this->idx >= 0){
                        std::memcpy(this->bytes, other.bytes, max_size); 
                    }
                }
                Result& operator=(Result&& other) noexcept{
                    if(this->idx >= 0){
                        this->try_destroy<0>();
                    }
                    this->idx = std::exchange(other.idx, -1);
                    if(this->idx >= 0){
                        std::memcpy(this->bytes, other.bytes, max_size); 
                    }
                    return *this;
                }
                ~Result(){
                    if(this->idx >= 0){
                        this->try_destroy<0>();
                    }
                }
                int who() const noexcept { return this->idx; }
                
                template<size_t I>
                struct type{
                    using nth_type = typename std::tuple_element<I, std::tuple<Ts...>>::type;
                };
                template<size_t I>
                using type_t = typename type<I>::nth_type;

                template<size_t I>
                type_t<I>& get() noexcept{
                    return reinterpret_cast<type_t<I>&>(bytes);
                }
                template<size_t I>
                const type_t<I>& get() const noexcept{
                    return reinterpret_cast<const type_t<I>&>(bytes);
                }
            };

            template<size_t hit, size_t i>
            void cancel_except() noexcept{
                if constexpr(i < num_ops){
                    if constexpr(i != hit && requires{std::get<i>(ops).cancel();}){
                        (void)std::get<i>(ops).cancel();
                    }
                    return cancel_except<hit, i+1>();
                }
            }

            template<size_t... Is>
            void start_all_tasks(std::index_sequence<Is...>) noexcept{
                (worker<Is>(*this, std::get<Is>(this->ops)), ...);
            }

            template<size_t worker_id, class Op>
            static xnet::detached_task worker(AnyAwaiter& awaiter, Op& op) noexcept{
                auto result = co_await op;
                if constexpr(fastfail){
                    // race
                    // side effects
                    bool need_cancel = false;
                    bool need_resume = false;
                    
                    int seen = awaiter.done.fetch_add(2, std::memory_order_acq_rel);
                    if(seen == 0){
                        // first
                        awaiter.result.template get<worker_id>() = std::move(result);
                        awaiter.result.idx = worker_id;
                        int post_seen = awaiter.done.fetch_sub(1, std::memory_order_acq_rel);
                        if(post_seen == num_ops * 2){
                            need_resume = true;
                        }
                        else{
                            need_cancel = true;
                        }
                    }
                    else{
                        if(seen + 2 == num_ops * 2 - 1){
                            need_resume = true;
                        }
                    }

                    if(need_cancel){ 
                        awaiter.cancel_except<worker_id, 0>(); 
                    }
                    if(need_resume){
                        awaiter.handler.resume();
                    }
                }
                else{
                    // any
                    // postive represents success, <= 0 for error
                    static_assert(requires{result.error();}, 
                    "AnyAwaiter<false, ...>::worker(): co_await result should have error() interface when disabling fastfail");

                    // side effects
                    bool ok = !result.error();
                    bool need_cancel = false;
                    bool need_resume = false;

                    int seen = awaiter.done.load(std::memory_order_acquire);
                    while(true){
                        int new_val;
                        need_cancel = false;
                        if(ok){
                            if(seen > 0){
                                new_val = seen + 2;
                            }
                            else{
                                need_cancel = true;
                                new_val = 2 - seen;
                            }
                        }
                        else{
                            new_val = (seen > 0 ? seen + 2 : seen - 2);
                        }
                        if(awaiter.done.compare_exchange_weak(seen, new_val, std::memory_order_acq_rel)){
                            need_resume = (new_val == (num_ops * 2 - 1)) || (-new_val == num_ops * 2);
                            break;
                        }
                    }

                    if(need_cancel){
                        awaiter.result.template get<worker_id>() = std::move(result);
                        awaiter.result.idx = worker_id;
                        int post_seen = awaiter.done.fetch_sub(1, std::memory_order_acq_rel);
                        if(post_seen == num_ops * 2){
                            need_resume = true;
                        }
                        else{
                            awaiter.cancel_except<worker_id, 0>(); 
                        }
                    }
                    if(need_resume){
                        awaiter.handler.resume();
                    }
                }
            }

            std::tuple<Ops...> ops;
            Result<await_result_t<Ops>...> result;
            std::coroutine_handle<> handler;
            std::atomic<int> done;
        public:
            static constexpr size_t num_ops = sizeof...(Ops);

            AnyAwaiter(Ops&&... ops) noexcept: ops(std::forward<Ops>(ops)...), result({}), handler(nullptr), done(0)
            {}
            AnyAwaiter(AnyAwaiter&& other) 
            noexcept: ops(std::move(other.ops)), result(std::move(other.result)), 
            handler(std::exchange(other.handler, nullptr)), done(other.done.load(std::memory_order_relaxed))
            {}
            ~AnyAwaiter() = default;

            details::io_result<bool> cancel() noexcept{
                if(this->handler == nullptr){
                    return {false, ECANCELED};
                }
                this->cancel_except<num_ops + 1, 0>();
                return {true, 0};
            }
            std::coroutine_handle<>& handle() noexcept{ return this->handler; }

            bool await_ready() const noexcept { return false; }

            template<class Promise>
            bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
                this->handler = handle;
                if constexpr(requires{ handle.promise().xcoro_hook(this); }){
                    if(!handle.promise().xcoro_hook(this)){
                        return false;
                    }
                }
                this->start_all_tasks(std::make_index_sequence<num_ops>());
                return true;
            }

            auto&& await_resume() noexcept{
                this->handler = nullptr;
                return std::move(this->result);
            }
        };
    };

    template<bool multi_threaded = true, class... Ops>
    auto race(Ops&&... ops) noexcept{
        static_assert(sizeof...(Ops) != 0, "race(Ops&&...): Please provide the arguments!");
        return details::AnyAwaiter<multi_threaded, true, Ops...>(std::forward<Ops>(ops)...);
    }

    template<bool multi_threaded = true, class... Ops>
    auto any(Ops&&... ops) noexcept{
        static_assert(sizeof...(Ops) != 0, "any(Ops&&...): Please provide the arguments!");
        return details::AnyAwaiter<multi_threaded, false, Ops...>(std::forward<Ops>(ops)...);
    }

    template<bool multi_threaded = true, class... Ops>
    auto all(Ops&&... ops) noexcept{
        static_assert(sizeof...(Ops) != 0, "all(Ops&&...): Please provide the arguments!");
        return details::AllAwaiter<multi_threaded, true, Ops...>(std::forward<Ops>(ops)...);
    }

    template<bool multi_threaded = true, class... Ops>
    auto allSettled(Ops&&... ops) noexcept{
        static_assert(sizeof...(Ops) != 0, "allSettled(Ops&&...): Please provide the arguments!");
        return details::AllAwaiter<multi_threaded, false, Ops...>(std::forward<Ops>(ops)...);
    }

    template<class T>
    xnet::detached_task fire(T awaitable) noexcept{
        co_await awaitable;
    }

    inline sockaddr_in v4addr(const char* ip, uint16_t port) noexcept {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip, &addr.sin_addr);
        return addr;
    }

    inline unsigned default_ipv6_interface() noexcept {
        struct ifaddrs* ifaddr = nullptr;
        if (getifaddrs(&ifaddr) == -1)
            return 0;

        unsigned fallback = 0;
        unsigned best = 0;

        for (auto* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr)
                continue;

            if (ifa->ifa_addr->sa_family != AF_INET6)
                continue;

            auto* sa6 = reinterpret_cast<sockaddr_in6*>(ifa->ifa_addr);

            if (ifa->ifa_flags & IFF_LOOPBACK)
                continue;

            const unsigned char* a = sa6->sin6_addr.s6_addr;
            bool is_link_local = (a[0] == 0xfe) && ((a[1] & 0xc0) == 0x80);

            unsigned idx = if_nametoindex(ifa->ifa_name);
            if (idx == 0)
                continue;

            if (is_link_local) {
                best = idx;
                break;
            }

            if (fallback == 0)
                fallback = idx;
        }

        freeifaddrs(ifaddr);
        return best ? best : fallback;
    }


    inline sockaddr_in6 v6addr(const char* ip, uint16_t port, uint32_t flowinfo = 0, uint32_t scope_id = 0) noexcept
    {
        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons(port);
        addr.sin6_flowinfo = flowinfo;
        addr.sin6_scope_id = scope_id;

        inet_pton(AF_INET6, ip, &addr.sin6_addr);

        const auto& a = addr.sin6_addr.s6_addr;

        bool is_link_local = (a[0] == 0xfe) && ((a[1] & 0xc0) == 0x80);

        if (scope_id == 0 && is_link_local) {
            addr.sin6_scope_id = if_nametoindex("eth0");
        }

        return addr;
    }

    class io_context{
        io_uring ring{};
        size_t evs_count;
    #ifndef XNET_DISABLE_THREAD_SAFE
        std::atomic_flag spinlock = ATOMIC_FLAG_INIT;

        auto& get_lock() noexcept{ return this->spinlock; }
    #endif

        struct NopAwaiter{
            io_context* ctx;
            std::coroutine_handle<> handler;
        public:
            bool await_ready() noexcept{ return false; }

            template<class Promise>
            bool await_suspend(std::coroutine_handle<Promise> h) noexcept{
                this->handler = h;
                auto& result = io_context::result();

                if constexpr(details::THREAD_SAFE_REQUIRED){
                    this->ctx->lock();
                }
                
                bool suspended = false;
                io_uring_sqe* sqe = io_uring_get_sqe(&this->ctx->ring);
                if(sqe != nullptr){
                    io_uring_prep_nop(sqe);
                    io_uring_sqe_set_data(sqe, h.address());

                    this->ctx->add_events(1);
                    suspended = true;
                    goto FINALLY;
                }
                this->handler = nullptr;
                result = -EAGAIN;
            FINALLY:
                if constexpr(details::THREAD_SAFE_REQUIRED){
                    this->ctx->unlock();
                }
                return suspended;
            }

            details::io_result<size_t> await_resume() noexcept{
                int result = io_context::result();
                int err = result < 0 ? -result : 0;
                size_t r = result < 0 ? 0 : static_cast<size_t>(result);

                if(this->handler){
                    this->handler = nullptr;
                    this->ctx->sub_events(1);
                }
                return details::io_result<size_t>(r, err);
            }

            io_context& sender() noexcept{ return *this->ctx; }
            bool pending() const noexcept { return this->handler != nullptr; }
            std::coroutine_handle<>& handle() noexcept{ return this->handler; }
        };
    public:
        static constexpr unsigned int default_flags = IORING_SETUP_CLAMP;

        io_context(unsigned entries = 1024, unsigned int flags = default_flags) noexcept: ring{}, evs_count(0){
            int err = io_uring_queue_init(entries, &ring, flags);
            if(err < 0){
                ring.ring_fd = -1;
            }
        }

        io_context(unsigned entries, struct io_uring_params* params) noexcept: ring{}, evs_count(0){
            int err = io_uring_queue_init_params(entries, &ring, params);
            if (err < 0) {
                ring.ring_fd = -1;
            }
        }

        io_context(const io_context&) = delete;

        ~io_context(){
            if(ring.ring_fd != -1){
                io_uring_queue_exit(&ring);
            }
        }

        template<class F, F cancelFunc, class data_type = void*>
        static details::io_result<bool> cancel(io_context& ctx, std::coroutine_handle<> target, std::coroutine_handle<> callback = nullptr) noexcept{
            if(target != nullptr){
                if constexpr(details::THREAD_SAFE_REQUIRED){
                    ctx.lock();
                }
                io_uring_sqe* sqe = io_uring_get_sqe(&ctx.ring);
                bool submitted = false;
                if(sqe != nullptr){
                    cancelFunc(sqe, (data_type)target.address(), 0);
                    XNET_SET_FLAGS_SKIP_SUCCESS(sqe);
                    io_uring_sqe_set_data(sqe, callback.address());
                    submitted = true;
                }
                if constexpr(details::THREAD_SAFE_REQUIRED){
                    ctx.unlock();
                }
                return details::io_result<bool>(submitted, submitted ? 0 : EAGAIN);
            }
            return details::io_result<bool>(false, ECANCELED);
        }

        bool invalid() const noexcept { return ring.ring_fd == -1; }

        size_t add_events(size_t n) noexcept{
            return this->evs_count += n;
        }
        size_t sub_events(size_t n) noexcept{
            return this->evs_count -= n;
        }
        size_t num_evs() const noexcept {
            return this->evs_count;
        }

#ifndef XNET_DISABLE_THREAD_SAFE
        void lock() noexcept {
            while(spinlock.test_and_set(std::memory_order_acquire)){
                XNET_CPU_RELAX();
            }
        }
        void unlock() noexcept{
            spinlock.clear(std::memory_order_release);
        }
#endif

#ifdef XNET_DISABLE_THREAD_SAFE
        void lock() noexcept {}
        void unlock() noexcept{}
#endif

        static int& result() noexcept {
            static thread_local int r;
            return r;
        }

        io_uring* native() noexcept { return &ring; }

        int submit() noexcept{
            if constexpr(details::THREAD_SAFE_REQUIRED){
                this->lock();
                int ret = io_uring_submit(&this->ring);
                this->unlock();
                return ret;
            }
            else{
                return io_uring_submit(&this->ring);
            }
        }

        int submit_and_wait() noexcept{
            if constexpr(details::THREAD_SAFE_REQUIRED){
                this->lock();
                int ret = io_uring_submit_and_wait(&this->ring, 1);
                this->unlock();
                return ret;
            }
            else{
                return io_uring_submit_and_wait(&this->ring, 1);
            }
        }

        // basic scheduler for simplicity
        void run_until_complete() noexcept{
            if(this->evs_count == 0){
                return;
            }
            constexpr size_t CQE_BATCH = 512;
            io_uring_cqe* cqes[CQE_BATCH];

            this->submit_and_wait();
            int& result_slot = xnet::io_context::result();
            unsigned int num_batch = (this->ring.sq.ring_entries / 4);
            num_batch = num_batch <= 256 ? num_batch : 256;
            while(true){
                unsigned int count = io_uring_peek_batch_cqe(&this->ring, cqes, CQE_BATCH);
                for(unsigned int i = 0; i < count; i++){
                    io_uring_cqe* cqe = cqes[i];
                    auto handle = std::coroutine_handle<>::from_address(
                        io_uring_cqe_get_data(cqe)
                    );
                    if(handle){
                        result_slot = cqe->res;
                        handle.resume();
                    }
                    if((i + 1) % num_batch == 0){
                        this->submit();
                    }
                }
                io_uring_cq_advance(&this->ring, count);
                if(this->evs_count == 0){
                    return;
                }
                this->submit_and_wait();
            }
        }

        auto yield() noexcept{
            return NopAwaiter{this, nullptr};
        }

        template<int xdomain, int xtype, bool client>
        class AsyncStream;
        using v4TCPServer = AsyncStream<AF_INET, SOCK_STREAM, false>;
        using v4TCPClient = AsyncStream<AF_INET, SOCK_STREAM, true>;
        using v6TCPServer = AsyncStream<AF_INET6, SOCK_STREAM, false>;
        using v6TCPClient = AsyncStream<AF_INET6, SOCK_STREAM, true>;
        using v4UDPServer = AsyncStream<AF_INET, SOCK_DGRAM, false>;
        using v4UDPClient = AsyncStream<AF_INET, SOCK_DGRAM, true>;
        using v6UDPServer = AsyncStream<AF_INET6, SOCK_DGRAM, false>;
        using v6UDPClient = AsyncStream<AF_INET6, SOCK_DGRAM, true>;
        using STDIN = AsyncStream<0, -1, false>;
        using STDOUT = AsyncStream<1, -1, false>;
        using STDERR = AsyncStream<2, -1, false>;
        using AsyncFile = AsyncStream<0, 0, false>;

        using TCPServer = v4TCPServer;
        using TCPClient = v4TCPClient;
        using UDPServer = v4UDPServer;
        using UDPClient = v4UDPClient;

        template<int xdomain, int xtype, bool client>
        class AsyncStream{
            friend class io_context;
            io_context* ctx;
            int stream;
        public:
            static constexpr int domain = xdomain;
            static constexpr int type = xtype;
            static constexpr bool istcp = std::is_same<AsyncStream, v4TCPServer>::value
                                        || std::is_same<AsyncStream, v6TCPServer>::value
                                        || std::is_same<AsyncStream, v4TCPClient>::value
                                        || std::is_same<AsyncStream, v6TCPClient>::value;
            static constexpr bool isudp = std::is_same<AsyncStream, v4UDPServer>::value
                                        || std::is_same<AsyncStream, v6UDPServer>::value
                                        || std::is_same<AsyncStream, v4UDPClient>::value
                                        || std::is_same<AsyncStream, v6UDPClient>::value;
            static constexpr bool isclient = client;
            static constexpr bool isserver = !client;
            static constexpr bool isstdin = std::is_same<AsyncStream, STDIN>::value;
            static constexpr bool isstdout = std::is_same<AsyncStream, STDOUT>::value;
            static constexpr bool isstderr = std::is_same<AsyncStream, STDERR>::value;
            static constexpr bool isfile = std::is_same<AsyncStream, AsyncFile>::value;
            static constexpr bool islocal = (domain == AF_UNIX) || (domain == AF_LOCAL);
            static constexpr int initfd = isstdin ? STDIN_FILENO : (isstdout ? STDOUT_FILENO : (isstderr ? STDERR_FILENO : INVALID_HANDLE));

            using addr_type = typename details::GET_ADDRTYPE<domain, 0>::type;

            AsyncStream() noexcept: ctx(nullptr), stream(INVALID_HANDLE){}
            AsyncStream(io_context& ctx, int fd) noexcept: ctx(&ctx), stream(fd){}
            AsyncStream(io_context& ctx, const addr_type* addr = nullptr) noexcept: ctx(&ctx), stream(initfd)
            {
                constexpr bool judge = AsyncStream::isclient || AsyncStream::isstdin || AsyncStream::isudp
                                    || AsyncStream::isstdout || AsyncStream::isstderr;
                static_assert(judge,
                "AsyncStream::AsyncStream(io_context&, const addr_type*) only UDP, STDXXXs and TCPClient can use this constructor");
                if constexpr(isstdin || isstdout || isstderr){
                    // int oldopt = fcntl(this->stream, F_GETFL);
                    // if(oldopt >= 0 && fcntl(this->stream, F_SETFL, oldopt | O_NONBLOCK) != SOCKET_ERROR){
                    //     return;
                    // }
                    // this->stream = INVALID_HANDLE;
                }
                else{
                    this->stream = socket(domain, type | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
                    if(this->stream != INVALID_HANDLE){
                        if constexpr(AsyncStream::istcp){
                            // tcp client
                            int flag = 1;
                            if(setsockopt(stream, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) != 0){
                                ::close(this->stream);
                                this->stream = INVALID_HANDLE;
                                return;
                            }
                        }
                        if constexpr(isserver){
                            // udp server
                            if(::bind(this->stream, (const sockaddr*)addr, sizeof(addr_type)) == SOCKET_ERROR){
                                ::close(this->stream);
                                this->stream = INVALID_HANDLE;
                                return;
                            }
                        }
                    }
                }
            }

            AsyncStream(const AsyncStream&) = delete;
            AsyncStream(AsyncStream&& other) noexcept: ctx(other.ctx), stream(std::exchange(other.stream, INVALID_HANDLE))
            {}
            void operator=(AsyncStream&& other) = delete;
            ~AsyncStream(){
                if(stream != INVALID_HANDLE){
                    ::close(this->stream);
                }
            }
        private:
            template<auto prep_func>
            class [[nodiscard]] IOAwaiter{
                using args_type = typename details::args_traits<prep_func>::args_type;
                static constexpr size_t num_args = std::tuple_size<args_type>::value;

                template<size_t i, class T, typename... Args, typename std::enable_if<(i == num_args - 1), bool>::type = true>
                static consteval bool check_args(){
                    return std::is_convertible<typename std::decay<T>::type, decltype(std::get<i>(std::declval<args_type>()))>::value;
                }

                template<size_t i, class T, typename... Args, typename std::enable_if<(i != num_args - 1), bool>::type = true>
                static consteval bool check_args(){
                    return std::is_convertible<typename std::decay<T>::type, decltype(std::get<i>(std::declval<args_type>()))>::value 
                    && check_args<i+1, Args...>();
                }

                template<size_t... I>
                void prep(io_uring_sqe* sqe, std::index_sequence<I...>) noexcept{
                    return prep_func(sqe, this->stream.stream, std::get<I>(args)...); 
                }

                class [[nodiscard]] IOTimeoutAwaiter{
                    AsyncStream& stream;
                    std::coroutine_handle<> handler;
                    args_type args;
                    __kernel_timespec ts;

                    template<size_t... I>
                    void prep(io_uring_sqe* sqe, std::index_sequence<I...>) noexcept{
                        return prep_func(sqe, this->stream.stream, std::get<I>(args)...); 
                    }
                
                public:
                    template<class T>
                    IOTimeoutAwaiter(AsyncStream& stream, uint32_t s, uint32_t ns, T&& args) 
                    noexcept: stream(stream), handler(nullptr), args(std::forward<T>(args)), ts({s, ns})
                    {}
                    IOTimeoutAwaiter(const IOTimeoutAwaiter& other) = delete;
                    IOTimeoutAwaiter(IOTimeoutAwaiter&& other) 
                    noexcept: stream(other.stream), handler(std::exchange(other.handler, nullptr)), args(other.args), ts{other.ts}
                    {}
                    ~IOTimeoutAwaiter() = default;

                    AsyncStream& sender() noexcept{ return this->stream; }
                    bool pending() const noexcept { return this->handler != nullptr; }
                    std::coroutine_handle<>& handle() noexcept{ return this->handler; }


                    details::io_result<bool> cancel() noexcept{
                        if(this->handler == nullptr){
                            return {false, ECANCELED};
                        }

                        if constexpr(details::is_poll_add<prep_func>::value){
                            using func_type = decltype(details::wrap_io_uring_prep_poll_remove);
                            using data_type = unsigned long long;
                            return io_context::cancel<func_type, details::wrap_io_uring_prep_poll_remove, data_type>(
                                *this->stream.ctx, this->handler
                            );
                        }
                        using func_type = decltype(details::wrap_io_uring_prep_cancel);
                        using data_type = void*;
                        return io_context::cancel<func_type, details::wrap_io_uring_prep_cancel, data_type>(
                            *this->stream.ctx, this->handler
                        );
                    }

                    bool await_ready() noexcept{ return false; }

                    template<class Promise>
                    bool await_suspend(std::coroutine_handle<Promise> h) noexcept{
                        this->handler = h;
                        auto& result = io_context::result();
                        if constexpr(requires{ h.promise().xcoro_hook(this); }){
                            if(!h.promise().xcoro_hook(this)){
                                this->handler = nullptr;
                                result = -ECANCELED;
                                return false;
                            }
                        }

                        if constexpr(details::THREAD_SAFE_REQUIRED){
                            this->stream.ctx->lock();
                        }

                        bool suspended = false;
                        unsigned left = io_uring_sq_space_left(&this->stream.ctx->ring);
                        if(left >= 2){
                            io_uring_sqe* sqe1 = io_uring_get_sqe(&this->stream.ctx->ring);
                            io_uring_sqe* sqe2 = io_uring_get_sqe(&this->stream.ctx->ring);
                            // prep something
                            this->prep(sqe1, std::make_index_sequence<num_args>());
                            io_uring_sqe_set_flags(sqe1, IOSQE_IO_LINK);
                            io_uring_sqe_set_data(sqe1, h.address());
                            io_uring_prep_link_timeout(sqe2, &this->ts, XNET_ETIME_SUCCESS);
                            io_uring_sqe_set_data(sqe2, nullptr);
                            XNET_SET_FLAGS_SKIP_SUCCESS(sqe2);

                            this->stream.ctx->add_events(1);
                            suspended = true;
                            goto FINALLY;
                        }
                        this->handler = nullptr;
                        result = -EAGAIN;
                    FINALLY:
                        if constexpr(details::THREAD_SAFE_REQUIRED){
                            this->stream.ctx->unlock();
                        }
                        return suspended;
                    }

                    details::io_result<size_t> await_resume() noexcept{
                        int result = io_context::result();
                        int err = result < 0 ? -result : 0;
                        size_t r = result < 0 ? 0 : static_cast<size_t>(result);

                        if(this->handler){
                            this->handler = nullptr;
                            this->stream.ctx->sub_events(1);
                        }
                        return details::io_result<size_t>(r, err);
                    }
                };

                AsyncStream& stream;
                std::coroutine_handle<> handler;
                args_type args;
            public:
                template<typename... Args>
                IOAwaiter(AsyncStream& stream, Args&&... args) 
                noexcept: stream(stream), handler(nullptr), args(std::tuple<Args...>(std::forward<Args>(args)...))
                {
                    static_assert((sizeof...(Args) == num_args) && check_args<0, Args...>(),
                    "IOAwaiter<>::(AysncStream& stream, Args&&...): args type mismatch with the io_func.");
                }
                IOAwaiter(const IOAwaiter& other) = delete;
                IOAwaiter(IOAwaiter&& other) noexcept: stream(other.stream), handler(std::exchange(other.handler, nullptr)), args(other.args)
                {}
                ~IOAwaiter() = default;

                AsyncStream& sender() noexcept{ return this->stream; }
                bool pending() const noexcept { return this->handler != nullptr; }
                std::coroutine_handle<>& handle() noexcept { return this->handler; }

                auto timeout(uint32_t s, uint32_t ns = 0) & noexcept{ 
                    return IOTimeoutAwaiter(this->stream, s, ns, this->args); 
                }
                auto timeout(uint32_t s, uint32_t ns = 0) && noexcept{ 
                    return IOTimeoutAwaiter(this->stream, s, ns, std::move(this->args)); 
                }

                details::io_result<bool> cancel() noexcept{
                    if(this->handler == nullptr){
                        return {false, ECANCELED};
                    }

                    if constexpr(details::is_poll_add<prep_func>::value){
                        using func_type = decltype(details::wrap_io_uring_prep_poll_remove);
                        using data_type = unsigned long long;
                        return io_context::cancel<func_type, details::wrap_io_uring_prep_poll_remove, data_type>(
                            *this->stream.ctx, this->handler
                        );
                    }

                    using func_type = decltype(details::wrap_io_uring_prep_cancel);
                    using data_type = void*;
                    return io_context::cancel<func_type, details::wrap_io_uring_prep_cancel, data_type>(
                        *this->stream.ctx, this->handler
                    );
                }

                bool await_ready() noexcept{ return false; }

                template<class Promise>
                bool await_suspend(std::coroutine_handle<Promise> h) noexcept{
                    this->handler = h;
                    auto& result = io_context::result();
                    if constexpr(requires{ h.promise().xcoro_hook(this); }){
                        if(!h.promise().xcoro_hook(this)){
                            this->handler = nullptr;
                            result = -ECANCELED;
                            return false;
                        }
                    }

                    if constexpr(details::THREAD_SAFE_REQUIRED){
                        this->stream.ctx->lock();
                    }
                    
                    bool suspended = false;
                    io_uring_sqe* sqe = io_uring_get_sqe(&this->stream.ctx->ring);
                    if(sqe != nullptr){
                        // prep something
                        this->prep(sqe, std::make_index_sequence<num_args>());
                        io_uring_sqe_set_data(sqe, h.address());

                        this->stream.ctx->add_events(1);
                        suspended = true;
                        goto FINALLY;
                    }
                    this->handler = nullptr;
                    result = -EAGAIN;
                FINALLY:
                    if constexpr(details::THREAD_SAFE_REQUIRED){
                        this->stream.ctx->unlock();
                    }
                    return suspended;
                }

                details::io_result<size_t> await_resume() noexcept{
                    int result = io_context::result();
                    int err = result < 0 ? -result : 0;
                    size_t r = result < 0 ? 0 : static_cast<size_t>(result);
                    if(this->handler){
                        this->handler = nullptr;
                        this->stream.ctx->sub_events(1);
                    }
                    return details::io_result<size_t>(r, err);
                }
            };
        public:
            void shutdown(int how = SHUT_RDWR) noexcept{
                ::shutdown(this->stream, how);
            }
            void close() noexcept{
                if(stream != INVALID_HANDLE){
                    ::close(this->stream);
                    stream = INVALID_HANDLE;
                }
            }

            bool invalid() const noexcept{ return this->stream == INVALID_HANDLE; }
            io_context& context() noexcept { return *this->ctx; }
            int& fd() noexcept { return this->stream; }
            void rebind_context(io_context& other_ctx) noexcept { this->ctx = &other_ctx; }

            addr_type sock_addr() const noexcept{
                if(this->stream != INVALID_HANDLE){
                    addr_type addr;
                    socklen_t size = sizeof(addr);
                    if(getsockname(stream, (sockaddr*)&addr, &size) == 0){
                        return addr;
                    }
                }
                return addr_type();
            }
            addr_type peer_addr() const noexcept{
                if(this->stream != INVALID_HANDLE){
                    addr_type addr;
                    socklen_t size = sizeof(addr);
                    if(getpeername(stream, (sockaddr*)&addr, &size) == 0){
                        return addr;
                    }
                }
                return addr_type{};
            }

            using AwaitablePollAdd = IOAwaiter<details::wrap_io_uring_prep_poll_add>;
            using AwaitableSendto = IOAwaiter<details::wrap_io_uring_prep_sendto>;
            using AwaitableSendMsg = IOAwaiter<details::wrap_io_uring_prep_sendmsg>;
            using AwaitableSend = IOAwaiter<details::wrap_io_uring_prep_send>;
            using AwaitableWriteV = IOAwaiter<details::wrap_io_uring_prep_writev>;
            using AwaitableWriteFixed = IOAwaiter<details::wrap_io_uring_prep_write_fixed>;
            using AwaitableWrite = IOAwaiter<details::wrap_io_uring_prep_write>;
            using AwaitableSendfile = IOAwaiter<details::wrap_io_uring_prep_sendfile>;
            using AwaitableConnect = IOAwaiter<details::wrap_io_uring_prep_connect>;

            using AwaitableReadFixed = IOAwaiter<details::wrap_io_uring_prep_read_fixed>;
            using AwaitableReadV = IOAwaiter<details::wrap_io_uring_prep_readv>;
            using AwaitableRecvMsg = IOAwaiter<details::wrap_io_uring_prep_recvmsg>;
            using AwaitableRecv = IOAwaiter<details::wrap_io_uring_prep_recv>;
            using AwaitableRead = IOAwaiter<details::wrap_io_uring_prep_read>;
            
            using AwaitableFsync = IOAwaiter<details::wrap_io_uring_prep_fsync>;
            using AwaitableFallocate = IOAwaiter<details::wrap_io_uring_prep_fallocate>;
            using AwaitableFadvise = IOAwaiter<details::wrap_io_uring_prep_fadvise>;


            AwaitablePollAdd poll_add(unsigned int poll_mask) noexcept{
                return AwaitablePollAdd(*this, poll_mask);
            }
            AwaitablePollAdd readable() noexcept{
                return AwaitablePollAdd(*this, POLL_IN);
            }
            AwaitablePollAdd writable() noexcept{
                return AwaitablePollAdd(*this, POLL_OUT);
            }
            AwaitableSendto sendto(const void* buf, size_t len, int flags, const struct sockaddr* addr, socklen_t addlen) noexcept { 
                return AwaitableSendto(*this, buf, len, flags | MSG_NOSIGNAL, addr, addlen);
            }
            AwaitableSendMsg sendmsg(msghdr* msg, unsigned int flags) noexcept { 
                return AwaitableSendMsg(*this, msg, flags | MSG_NOSIGNAL);
            }
            AwaitableSend send(const void* buf, size_t len, int flags) noexcept {
                return AwaitableSend(*this, buf, len, flags | MSG_NOSIGNAL); 
            }
            AwaitableWriteV writev(const struct iovec* iovecs, unsigned int nr_vecs, unsigned long long offset) noexcept{
                return AwaitableWriteV(*this, iovecs, nr_vecs, offset);
            }
            AwaitableWriteFixed write_fixed(const struct iovec* iovecs, unsigned int nr_vecs, unsigned long long offset, int buf_index) noexcept{
                return AwaitableWriteFixed(*this, iovecs, nr_vecs, offset, buf_index);
            }
            AwaitableWrite write(const void* buf, unsigned int nbytes, unsigned long long offset) noexcept{
                return AwaitableWrite(*this, buf, nbytes, offset);
            }
            AwaitableSendfile sendfile(int filefd, int64_t offset, unsigned int bytes, unsigned int flags) noexcept {
                return AwaitableSendfile(*this, filefd, offset, bytes, flags); 
            }

            template<class T>
            requires std::same_as<T, addr_type>
            AwaitableConnect connect(const T* addr, [[maybe_unused]] socklen_t addrlen) noexcept{
                static_assert(AsyncStream::isclient, 
                "AysncStream<>::connect(...): only client can use this function.");
                if(this->stream == INVALID_HANDLE){ 
                    this->stream = socket(domain, type | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
                    if constexpr(AsyncStream ::istcp){
                        if(this->stream != INVALID_HANDLE){
                            int one = 1;
                            if(setsockopt(stream, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) != 0){
                                ::close(this->stream);
                                this->stream = INVALID_HANDLE;
                            }
                        }
                    }
                }
                return AwaitableConnect(*this, (const sockaddr*)addr, sizeof(addr_type));
            }

            AwaitableReadFixed read_fixed(const struct iovec* iovecs, unsigned int nr_vecs, unsigned long long offset, int buf_index) noexcept{
                return AwaitableReadFixed(*this, iovecs, nr_vecs, offset, buf_index);
            }
            AwaitableReadV readv(const struct iovec* iovecs, unsigned int nr_vecs, unsigned long long offset) noexcept{
                return AwaitableReadV(*this, iovecs, nr_vecs, offset);
            }
            AwaitableRecvMsg recvmsg(msghdr* msg, unsigned int flags) noexcept { 
                return AwaitableRecvMsg(*this, msg, flags); 
            }
            AwaitableRecv recv(void* buf, size_t len, int flags) noexcept { 
                return AwaitableRecv(*this, buf, len, flags); 
            }
            AwaitableRead read(void* buf, unsigned int nbytes, unsigned long long offset) noexcept{
                return AwaitableRead(*this, buf, nbytes, offset);
            }

            // async file
            AwaitableFsync fsync(unsigned int flags = 0) noexcept{
                static_assert(AsyncStream::isfile, 
                "AsyncStream<>:fsync(...): only AsyncFile type can use this function.");
                return AwaitableFsync(*this, flags);
            }
            AwaitableFallocate fallocate(int mode, unsigned long long offset, unsigned long long len) noexcept{
                static_assert(AsyncStream::isfile, 
                "AsyncStream<>:fallocate(...): only AsyncFile type can use this function.");
                return AwaitableFallocate(*this, mode, offset, len);
            }
            AwaitableFadvise fadvise(unsigned long long offset, off_t len, int advice) noexcept{
                static_assert(AsyncStream::isfile, 
                "AsyncStream<>:fadvise(...): only AsyncFile type can use this function.");
                return AwaitableFadvise(*this, offset, len, advice);
            }
        };
    
        template<int xdomain, int xtype>
        class AsyncAccepter;
        using v4TCPAccepter = AsyncAccepter<AF_INET, SOCK_STREAM>;
        using v6TCPAccepter = AsyncAccepter<AF_INET6, SOCK_STREAM>;
        using TCPAccepter = v4TCPAccepter;

        template<int xdomain, int xtype>
        class AsyncAccepter{
            friend class io_context;
            io_context* ctx;
            int server;

            static int set_default_opt(int fd) noexcept{
                int one = 1;
                if(::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) == 0){
                    return 0;
                }
                return -1;
            }

        public:
            static constexpr int domain = xdomain;
            static constexpr int type = xtype;
            using addr_type = typename details::GET_ADDRTYPE<domain, 0>::type;
            using tcp_type = AsyncStream<domain, type, false>;

            AsyncAccepter(io_context& ctx, const addr_type& addr, bool reuseport = false, int maxlisten=SOMAXCONN)
            noexcept: ctx(&ctx), server(socket(domain, type | SOCK_NONBLOCK | SOCK_CLOEXEC, 0))
            {
                static_assert(xtype == SOCK_STREAM || xtype == SOCK_SEQPACKET, 
                "AsyncAccepter<xdomain, xtype>::AsyncAccepter(io_context&, uint16_t, const addr_type&, int): only accept SOCK_STREAM or SOCK_SEQPACKET type.");
                if(server != INVALID_HANDLE){
                    if(!reuseport || (this->set_default_opt(this->server) == 0)){
                        if(::bind(server, (const sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR){
                            if(::listen(server, maxlisten) != SOCKET_ERROR){
                                return;
                            }
                        }
                    }
                    ::close(server);
                    server = INVALID_HANDLE;
                }
            }

            AsyncAccepter(const AsyncAccepter&) = delete;
            AsyncAccepter(AsyncAccepter&& other) noexcept: ctx(other.ctx), server(std::exchange(other.server, INVALID_HANDLE))
            {}
            void operator=(AsyncAccepter&& other) = delete;
            ~AsyncAccepter(){
                if(server != INVALID_HANDLE){
                    ::close(this->server);
                }
            }
        private:
            class [[nodiscard]] AcceptAwaiter{
                AsyncAccepter& accepter;
                std::coroutine_handle<> handler;

                class [[nodiscard]] AcceptTimeoutAwaiter{
                    AsyncAccepter& accepter;
                    std::coroutine_handle<> handler;
                    __kernel_timespec ts;
                public:
                    AcceptTimeoutAwaiter(AsyncAccepter& accepter, uint32_t s, uint32_t ns) 
                    noexcept: accepter(accepter), handler(nullptr), ts({s, ns})
                    {}
                    AcceptTimeoutAwaiter(const AcceptTimeoutAwaiter& other) = delete;
                    AcceptTimeoutAwaiter(AcceptTimeoutAwaiter&& other) 
                    noexcept: accepter(other.accepter), handler(std::exchange(other.handler, nullptr)), ts(other.ts)
                    {}

                    AsyncAccepter& sender() noexcept { return this->accepter; }
                    bool pending() const noexcept { return this->handler != nullptr; }
                    std::coroutine_handle<>& handle() noexcept { return this->handler; }

                    details::io_result<bool> cancel() noexcept{
                        if(this->handler == nullptr){
                            return {false, ECANCELED};
                        }
                        using func_type = decltype(details::wrap_io_uring_prep_cancel);
                        using data_type = void*;
                        return io_context::cancel<func_type, details::wrap_io_uring_prep_cancel, data_type>(
                            *this->accepter.ctx, this->handler
                        );
                    }

                    bool await_ready() const noexcept{ return false; }

                    template<class Promise>
                    bool await_suspend(std::coroutine_handle<Promise> h) noexcept{
                        this->handler = h;
                        auto& result = io_context::result();
                        if constexpr(requires{ h.promise().xcoro_hook(this); }){
                            if(!h.promise().xcoro_hook(this)){
                                this->handler = nullptr;
                                result = -ECANCELED;
                                return false;
                            }
                        }

                        if constexpr(details::THREAD_SAFE_REQUIRED){
                            this->accepter.ctx->lock();
                        }

                        bool suspended = false;
                        unsigned left = io_uring_sq_space_left(&this->accepter.ctx->ring);
                        if(left >= 2){
                            io_uring_sqe* sqe1 = io_uring_get_sqe(&this->accepter.ctx->ring);
                            io_uring_sqe* sqe2 = io_uring_get_sqe(&this->accepter.ctx->ring);

                            io_uring_prep_accept(sqe1, this->accepter.server, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
                            io_uring_sqe_set_flags(sqe1, IOSQE_IO_LINK);
                            io_uring_sqe_set_data(sqe1, h.address());
                            io_uring_prep_link_timeout(sqe2, &this->ts, XNET_ETIME_SUCCESS);
                            io_uring_sqe_set_data(sqe2, nullptr);
                            XNET_SET_FLAGS_SKIP_SUCCESS(sqe2);

                            this->accepter.ctx->add_events(1);
                            suspended = true;
                            goto FINALLY;
                        }
                        this->handler = nullptr;
                        result = -EAGAIN;
                    FINALLY:
                        if constexpr(details::THREAD_SAFE_REQUIRED){
                            this->accepter.ctx->unlock();
                        }
                        return suspended;
                    }

                    details::io_result<tcp_type> await_resume() noexcept{
                        int result = io_context::result();
                        int err = 0;
                        if(result < 0){
                            err = -result;
                            result = INVALID_HANDLE;
                        }
                        else{
                            int flag = 1;
                            if(setsockopt(result, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) != 0){
                                ::close(result);
                                err = errno;
                                result = INVALID_HANDLE;
                            }
                        }
                        if(this->handler){
                            this->handler = nullptr;
                            this->accepter.ctx->sub_events(1);
                        }
                        return details::io_result<tcp_type>(tcp_type(*this->accepter.ctx, result), err);
                    }
                };

            public:
                AcceptAwaiter(AsyncAccepter& accepter) noexcept :accepter(accepter), handler(nullptr)
                {}
                AcceptAwaiter(const AcceptAwaiter& other) = delete;
                AcceptAwaiter(AcceptAwaiter&& other) 
                noexcept: accepter(other.accepter), handler(std::exchange(other.handler, nullptr))
                {}

                AsyncAccepter& sender() noexcept { return this->accepter; }
                bool pending() const noexcept { return this->handler != nullptr; }
                std::coroutine_handle<>& handle() noexcept { return this->handler; }

                auto timeout(uint32_t s, uint32_t ns = 0) const noexcept{ 
                    return AcceptTimeoutAwaiter(this->accepter, s, ns); 
                }

                details::io_result<bool> cancel() noexcept{
                    if(this->handler == nullptr){
                        return {false, ECANCELED};
                    }
                    using func_type = decltype(details::wrap_io_uring_prep_cancel);
                    using data_type = void*;
                    return io_context::cancel<func_type, details::wrap_io_uring_prep_cancel, data_type>(
                        *this->accepter.ctx, this->handler
                    );
                }

                bool await_ready() const noexcept{ return false; }

                template<class Promise>
                bool await_suspend(std::coroutine_handle<Promise> h) noexcept{                    
                    this->handler = h;
                    auto& result = io_context::result();
                    if constexpr(requires{ h.promise().xcoro_hook(this); }){
                        if(!h.promise().xcoro_hook(this)){
                            this->handler = nullptr;
                            result = -ECANCELED;
                            return false;
                        }
                    }

                    if constexpr(details::THREAD_SAFE_REQUIRED){
                        this->accepter.ctx->lock();
                    }
                    
                    bool suspended = false;
                    io_uring_sqe* sqe = io_uring_get_sqe(&this->accepter.ctx->ring);
                    if(sqe != nullptr){
                        io_uring_prep_accept(sqe, this->accepter.server, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
                        io_uring_sqe_set_data(sqe, h.address());

                        this->accepter.ctx->add_events(1);
                        suspended = true;
                        goto FINALLY;
                    }
                    this->handler = nullptr;
                    result = -EAGAIN;
                FINALLY:
                    if constexpr(details::THREAD_SAFE_REQUIRED){
                        this->accepter.ctx->unlock();
                    }
                    return suspended;
                }

                details::io_result<tcp_type> await_resume() noexcept{
                    int result = io_context::result();
                    int err = 0;
                    if(result < 0){
                        err = -result;
                        result = INVALID_HANDLE;
                    }
                    else{
                        int flag = 1;
                        if(setsockopt(result, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) != 0){
                            ::close(result);
                            err = errno;
                            result = INVALID_HANDLE;
                        }
                    }
                    if(this->handler){
                        this->handler = nullptr;
                        this->accepter.ctx->sub_events(1);
                    }
                    return details::io_result<tcp_type>(tcp_type(*this->accepter.ctx, result), err);
                }
            };
        public:
            void shutdown(int how = SHUT_RDWR) noexcept{
                ::shutdown(this->server, how);
            }
            void close() noexcept{
                if(server != INVALID_HANDLE){
                    ::close(this->server);
                    server = INVALID_HANDLE;
                }
            }
            bool invalid() const noexcept { return this->server == INVALID_HANDLE; }
            io_context& context() noexcept { return *this->ctx; }
            void rebind_context(io_context& other_ctx) noexcept { this->ctx = &other_ctx; }
            int& fd() noexcept { return this->server; }

            AcceptAwaiter accept() noexcept{ return AcceptAwaiter(*this); }
        };

        class AsyncTimer{
            friend class io_context;
            io_context* ctx;
        public:
            AsyncTimer(io_context& ctx) noexcept: ctx(&ctx)
            {}
            AsyncTimer(const AsyncTimer&) = delete;
            AsyncTimer(AsyncTimer&& other) noexcept: ctx(other.ctx)
            {}
            void operator=(AsyncTimer&&) = delete;
            ~AsyncTimer() = default;
        private:
            class [[nodiscard]] Awaiter{
                AsyncTimer& timer;
                std::coroutine_handle<> handler;
                __kernel_timespec ts;
            public:
                Awaiter(AsyncTimer& timer, uint32_t s, uint32_t ns) noexcept: timer(timer), handler(), ts({s, ns})
                {}
                Awaiter(const Awaiter&) = delete;
                Awaiter(Awaiter&& other) noexcept: timer(other.timer), handler(std::exchange(other.handler, nullptr)), ts(other.ts)
                {}
                ~Awaiter() = default;

                AsyncTimer& sender() noexcept { return this->timer; }
                bool pending() const noexcept { return this->handler != nullptr; }
                std::coroutine_handle<>& handle() noexcept { return this->handler; }

                details::io_result<bool> cancel() noexcept{
                    if(this->handler == nullptr){
                        return {false, ECANCELED};
                    }
                    using func_type = decltype(details::wrap_io_uring_prep_timeout_remove);
                    using data_type = unsigned long long;
                    return io_context::cancel<func_type, details::wrap_io_uring_prep_timeout_remove, data_type>(
                        *this->timer.ctx, this->handler
                    );
                }

                bool await_ready() noexcept{ 
                    if(this->ts.tv_sec != 0 || this->ts.tv_nsec != 0){
                        return false;
                    }
                    io_context::result() = -ETIME;
                    return true;
                }

                template<class Promise>
                bool await_suspend(std::coroutine_handle<Promise> h) noexcept{
                    this->handler = h;
                    auto& result = io_context::result();
                    if constexpr(requires{ h.promise().xcoro_hook(this); }){
                        if(!h.promise().xcoro_hook(this)){
                            this->handler = nullptr;
                            result = -ECANCELED;
                            return false;
                        }
                    }

                    if constexpr(details::THREAD_SAFE_REQUIRED){
                        this->timer.ctx->lock();
                    }

                    bool suspended = false;
                    io_uring_sqe* sqe = io_uring_get_sqe(&this->timer.ctx->ring);
                    if(sqe != nullptr){
                        io_uring_prep_timeout(sqe, &this->ts, 0, 0);
                        io_uring_sqe_set_data(sqe, h.address());

                        this->timer.ctx->add_events(1);
                        suspended = true;
                        goto FINALLY;
                    }
                    result = errno;
                FINALLY:
                    if constexpr(details::THREAD_SAFE_REQUIRED){
                        this->timer.ctx->unlock();
                    }
                    return suspended;
                }

                details::io_result<void> await_resume() noexcept{
                    int result = io_context::result();
                    int err = result != -ETIME ? -result : 0;
                    if(this->handler){
                        this->handler = nullptr;
                        this->timer.ctx->sub_events(1);
                    }
                    return details::io_result<void>(err);
                }

            };
        public:
            io_context& context() noexcept { return *this->ctx; }
            void rebind_context(io_context& other_ctx) noexcept { this->ctx = &other_ctx; }

            Awaiter timeout(uint32_t s, uint32_t ns = 0) noexcept{
                return Awaiter(*this, s, ns);
            }
        };

        class AsyncFileSystem{
            friend class io_context;
            io_context* ctx;
        public:
            AsyncFileSystem(io_context& ctx) noexcept: ctx(&ctx)
            {}
            AsyncFileSystem(const AsyncFileSystem&) = delete;
            AsyncFileSystem(AsyncFileSystem&& other) noexcept: ctx(other.ctx)
            {}
            void operator=(AsyncFileSystem&&) = delete;
            ~AsyncFileSystem() = default;
        private:
            template<auto prep_func, class Ret>
            class [[nodiscard]] FileSystemAwaiter{
                using args_type = typename details::args_traits<prep_func>::args_type;
                static constexpr size_t num_args = std::tuple_size<args_type>::value;

                template<size_t i, class T, typename... Args, typename std::enable_if<(i == num_args - 1), bool>::type = true>
                static consteval bool check_args(){
                    return std::is_convertible<typename std::decay<T>::type, decltype(std::get<i>(std::declval<args_type>()))>::value;
                }

                template<size_t i, class T, typename... Args, typename std::enable_if<(i != num_args - 1), bool>::type = true>
                static consteval bool check_args(){
                    return std::is_convertible<typename std::decay<T>::type, decltype(std::get<i>(std::declval<args_type>()))>::value 
                    && check_args<i+1, Args...>();
                }

                template<size_t... I>
                void prep(io_uring_sqe* sqe, std::index_sequence<I...>) noexcept{
                    return prep_func(sqe, std::get<I>(args)...); 
                }

                AsyncFileSystem& filesystem;
                std::coroutine_handle<> handler;
                args_type args;
            public:
                template<typename... Args>
                FileSystemAwaiter(AsyncFileSystem& filesystem, Args&&... args) 
                noexcept: filesystem(filesystem), handler(nullptr), args(std::tuple<Args...>(std::forward<Args>(args)...))
                {
                    static_assert((sizeof...(Args) == num_args) && check_args<0, Args...>(),
                    "FileSystemAwaiter<>::(AsyncFileSystem&, Args&&...): args type mismatch with the system_func.");
                }
                FileSystemAwaiter(const FileSystemAwaiter& other) = delete;
                FileSystemAwaiter(FileSystemAwaiter&& other)
                noexcept: filesystem(other.filesystem), handler(std::exchange(other.handler, nullptr))
                {}
                ~FileSystemAwaiter() = default;

                AsyncFileSystem& sender(){ return this->filesystem; }
                bool pending() const noexcept { return this->handler != nullptr; }
                std::coroutine_handle<>& handle() noexcept { return this->handler; }

                details::io_result<bool> cancel() noexcept{
                    if(this->handler == nullptr){
                        return {false, ECANCELED};
                    }
                    using func_type = decltype(details::wrap_io_uring_prep_cancel);
                    using data_type = void*;
                    return io_context::cancel<func_type, details::wrap_io_uring_prep_cancel, data_type>(
                        *this->filesystem.ctx, this->handler
                    );
                }

                bool await_ready() noexcept { return false; }

                template<class Promise>
                bool await_suspend(std::coroutine_handle<Promise> h) noexcept{
                    this->handler = h;
                    auto& result = io_context::result();
                    if constexpr(requires{ h.promise().xcoro_hook(this); }){
                        if(!h.promise().xcoro_hook(this)){
                            this->handler = nullptr;
                            result = -ECANCELED;
                            return false;
                        }
                    }

                    if constexpr(details::THREAD_SAFE_REQUIRED){
                        this->filesystem.ctx->lock();
                    }

                    bool suspended = false;
                    io_uring_sqe* sqe = io_uring_get_sqe(&this->filesystem.ctx->ring);
                    if(sqe != nullptr){
                        this->prep(sqe, std::make_index_sequence<num_args>());
                        io_uring_sqe_set_data(sqe, h.address());

                        this->filesystem.ctx->add_events(1);
                        suspended = true;
                        goto FINALLY;
                    }
                    this->handler = nullptr;
                    result = -EAGAIN;
                FINALLY:
                    if constexpr(details::THREAD_SAFE_REQUIRED){
                        this->filesystem.ctx->unlock();
                    }
                    return suspended;
                }

                details::io_result<Ret> await_resume() noexcept{
                    int result = io_context::result();
                    int err = 0;
                    if(result < 0){
                        err = -result;
                        result = INVALID_HANDLE;
                    }
                    if(this->handler){
                        this->handler = nullptr;
                        this->filesystem.ctx->sub_events(1);
                    }
                    using T = details::io_result<Ret>;
                    if constexpr(std::is_same<Ret, AsyncFile>::value){
                        return T(AsyncFile(*this->filesystem.ctx, result), err);
                    }
                    else{
                        return T(result, err);
                    }
                }
            };
            using OpenAtAwaiter = FileSystemAwaiter<details::wrap_io_uring_prep_openat, AsyncFile>;
            using MkDirAtAwaiter = FileSystemAwaiter<details::wrap_io_uring_prep_mkdirat, int>;
            using RenameAtAwaiter = FileSystemAwaiter<details::wrap_io_uring_prep_renameat, int>;
            using UnlinkAtAwaiter = FileSystemAwaiter<details::wrap_io_uring_prep_unlinkat, int>;
            using StatxAtAwaiter = FileSystemAwaiter<details::wrap_io_uring_prep_statx, int>;
            using LinkAtAwaiter = FileSystemAwaiter<details::wrap_io_uring_prep_linkat, int>;
            using SymlinkAtAwaiter = FileSystemAwaiter<details::wrap_io_uring_prep_symlinkat, int>;
        public:
            io_context& context() noexcept { return *this->ctx; }
            void rebind_context(io_context& other_ctx) noexcept { this->ctx = &other_ctx; }

            OpenAtAwaiter openat(const char* path, int flags, mode_t mode = 0644, int dfd=AT_FDCWD) noexcept{
                return OpenAtAwaiter(*this, dfd, path, flags, mode);
            }
            MkDirAtAwaiter mkdirat(const char* path, mode_t mode = 0644, int dfd=AT_FDCWD) noexcept{
                return MkDirAtAwaiter(*this, dfd, path, mode);
            }
            RenameAtAwaiter renameat(const char* oldpath, const char* newpath, unsigned int flags = 0, int olddfd=AT_FDCWD, int newdfd=AT_FDCWD) noexcept{
                return RenameAtAwaiter(*this, olddfd, oldpath, newdfd, newpath, flags);
            }
            UnlinkAtAwaiter unlinkat(const char* path, int flags = 0, int dfd=AT_FDCWD) noexcept{
                return UnlinkAtAwaiter(*this, dfd, path, flags);
            }
            UnlinkAtAwaiter removeat(const char* path, int flags = 0, int dfd=AT_FDCWD) noexcept{
                return UnlinkAtAwaiter(*this, dfd, path, flags);
            }
            StatxAtAwaiter statx(const char* path, struct statx* stat, unsigned int mask = STATX_BASIC_STATS, int flags = 0, int dfd=AT_FDCWD) noexcept{
                return StatxAtAwaiter(*this, dfd, path, flags, mask, stat);
            }
            StatxAtAwaiter fstatx(AsyncFile& file, struct statx* stat, unsigned int mask = STATX_BASIC_STATS, int flags = 0) noexcept{
                return StatxAtAwaiter(*this, file.fd(), "", flags, mask, stat);
            }
            StatxAtAwaiter fstatx(int fd, struct statx* stat, unsigned int mask = STATX_BASIC_STATS, int flags = 0) noexcept{
                return StatxAtAwaiter(*this, fd, "", flags, mask, stat);
            }
            LinkAtAwaiter linkat(const char* oldpath, const char* newpath, int flags = 0, int olddfd = AT_FDCWD, int newdfd = AT_FDCWD) noexcept{
                return LinkAtAwaiter(*this, olddfd, oldpath, newdfd, newpath, flags);
            }
            SymlinkAtAwaiter symlinkat(const char* target, const char* linkpath, int newdfd = AT_FDCWD) noexcept{
                return SymlinkAtAwaiter(*this, target, newdfd, linkpath);
            }
        };
    };

    template<int xdomain, int xtype>
    using AsyncAccepter = io_context::AsyncAccepter<xdomain, xtype>;
    using v4TCPAccepter = io_context::v4TCPAccepter;
    using v6TCPAccepter = io_context::v6TCPAccepter;

    using AsyncFileSystem = io_context::AsyncFileSystem;

    using TCPAccepter = io_context::TCPAccepter;

    using AsyncTimer = io_context::AsyncTimer;

    template<int xdomain, int xtype, bool client>
    using AsyncStream = io_context::AsyncStream<xdomain, xtype, client>;

    using v4TCPServer = io_context::v4TCPServer;
    using v4TCPClient = io_context::v4TCPClient;
    using v6TCPServer = io_context::v6TCPServer;
    using v6TCPClient = io_context::v6TCPClient;
    using v4UDPServer = io_context::v4UDPServer;
    using v4UDPClient = io_context::v4UDPClient;
    using v6UDPServer = io_context::v6UDPServer;
    using v6UDPClient = io_context::v6UDPClient;
    using STDIN = io_context::STDIN;
    using STDOUT = io_context::STDOUT;
    using STDERR = io_context::STDERR;
    using AsyncFile = io_context::AsyncFile;
    using AsyncFileSystem = io_context::AsyncFileSystem;

    using TCPServer = io_context::TCPServer;
    using TCPClient = io_context::TCPClient;
    using UDPServer = io_context::UDPServer;
    using UDPClient = io_context::UDPClient;

    template<class T>
    using io_result = details::io_result<T>;
};


#endif
#endif