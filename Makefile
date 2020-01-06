.PHONY: all

all: server client client/client server/server

server:
	mkdir -p server

client:
	mkdir -p client

client/client: client.cpp
	g++ -Wall -g -O2 -std=c++17 client.cpp -o client/client -lboost_program_options

server/server: server.cpp
	g++ -Wall -g -O2 -std=c++17 server.cpp -o server/server -lboost_program_options

clean:
	rm -rf server client
