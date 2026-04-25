CXX := g++
CXXFLAGS := -O2 -std=c++20 -Wall -Wextra -I../include/
LDFLAGS := -luring
NO_THREAD := -DXNET_DISABLE_THREAD_SAFE
SKIP_SUCCESS := -DXNET_ENABLE_SKIP_SUCCESS
NO_EXCEPTIONS := -fno-exceptions -fno-rtti -flto -march=native
ASIO := -DASIO_NO_EXCEPTIONS -DASIO_STANDALONE -DASIO_NO_THROW -DASIO_NO_TYPEID -I./asio/asio/include -DASIO_HAS_IO_URING -DASIO_DISABLE_EPOLL -luring

EXAMPLES := xnet_echo_single echo_loadtest xnet_echo_multithread asio_echo_multithread

all: $(EXAMPLES)

xnet_echo_single: xnet_echo_single.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(NO_THREAD) $(NO_EXCEPTIONS) $(SKIP_SUCCESS)

echo_loadtest: echo_loadtest.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(NO_THREAD) $(NO_EXCEPTIONS) $(SKIP_SUCCESS)

xnet_echo_multithread: xnet_echo_multithread.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(NO_THREAD) $(NO_EXCEPTIONS) $(SKIP_SUCCESS)

asio_echo_multithread: asio_echo_multithread.cpp
	$(CXX) -O3 -std=c++20 -Wall -Wextra $< -o $@ $(ASIO) $(NO_EXCEPTIONS)

clean:
	rm -f $(EXAMPLES)