CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -pthread

all: bench

bench: main.cpp spsc.hpp
	$(CXX) $(CXXFLAGS) main.cpp -o bench

run: bench
	./bench

clean:
	rm -f bench

.PHONY: all run clean
