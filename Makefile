.PHONY: all

all: client.out server.out

client.out: client.cpp
	g++ -Wall -g -O2 -std=c++17 client.cpp -o client.out -lboost_program_options

server.out: server.cpp
	g++ -Wall -g -O2 -std=c++17 server.cpp -o server.out -lboost_program_options
