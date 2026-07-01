CXX = g++
CXXFLAGS = -std=c++17 -Wall -pthread
LDFLAGS = -lrt

all: server

server: src/blinkdb.cpp src/server.cpp
	$(CXX) $(CXXFLAGS) -o server $^ $(LDFLAGS)

clean:
	rm -f server
