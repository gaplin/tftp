.PHONY: all

all: server client client/client.out server/server.out

server:
	mkdir -p server

client:
	mkdir -p client

client/client.out: client.cpp
	g++ -Wall -g -O2 -std=c++17 client.cpp -o client/client.out -lboost_program_options

server/server.out: server.cpp
	g++ -Wall -g -O2 -std=c++17 server.cpp -o server/server.out -lboost_program_options
