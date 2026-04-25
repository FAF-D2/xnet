CXX := g++
CXXFLAGS := -O2 -std=c++20 -Wall -Wextra -I../include
LDFLAGS := -luring

EXAMPLES := async_combinators echo_client echo_server

all: $(EXAMPLES)

async_combinators: async_combinators.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

echo_client: echo_client.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

echo_server: echo_server.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(EXAMPLES)
