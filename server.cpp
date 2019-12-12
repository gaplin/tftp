// vim:ts=4:sts=4:sw=4:expandtab

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>

#include <gsl/gsl_errno.h>
#include <gsl/gsl_fft_complex.h>

#include <complex>
#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <ctime>
#include <fstream>
#include <string.h>
#include <cstdlib>
#include <unordered_map>
#include <fstream>

#define MAX_EVENTS 1000
#define BUFFER_SIZE 10000
const unsigned short RRQ = 1;
const unsigned short WRQ = 2;
const unsigned short DATA = 3;
const unsigned short ACK = 4;
const unsigned short ERROR = 5;

using namespace std;

class Client {
    private:
    int fd;
    struct sockaddr_in address;
    socklen_t ssize = sizeof(struct sockaddr_in);
    char buf[BUFFER_SIZE];
    int length = 0;
    fstream file;
    unsigned long long position = 0;
    string fileName;
    unsigned int blockSize = 512;

    friend void swap(Client& first, Client& second){
        using std::swap;
        swap(first.address, second.address);
        swap(first.fd, second.fd);
        swap(first.buf, second.buf);
        swap(first.length, second.length);
        swap(first.ssize, second.ssize);
        swap(first.fileName, second.fileName);
        swap(first.file, second.file);
        swap(first.blockSize, second.blockSize);
        swap(first.position, second.position);
    }

    public:

    Client() {}

    Client(int fd, struct sockaddr_in& addr, char buf[], int length){
        memmove(this->buf, buf, length);
        memmove(&address, &addr, sizeof(addr));
        this->length = length;
        fileName = GetString(2);
        this->fd = fd;
        if(GetType() == RRQ){
            file.open(fileName, ios::in);
        } else {
            file.open(fileName, ios::app);
        }
    }

    Client(const Client& other) {
        memmove(buf, other.buf, other.length);
        memmove(&address, &(other.address), sizeof(other.address));
        fd = other.fd;
        length = other.length;
        ssize = other.ssize;
        fileName = other.fileName;
        blockSize = other.blockSize;
        position = other.position;
        if(GetType() == RRQ){
            file.open(fileName, ios::in);
        } else {
            file.open(fileName, ios::app);
        }
    }

    Client(Client && other){
        swap(*this, other);
    }
    ~Client() {
        file.close();
    }

    Client& operator=(Client other) {
        swap(*this, other);
        return *this;
    }

    int Send(char buf[], int bytes) {
        return sendto(fd, buf, bytes, 0, (sockaddr*)&address, ssize);
    }

    int Recv() {
        length = recvfrom(fd, buf, BUFFER_SIZE, 0, (sockaddr*)&address, &ssize);
        return length;
    }

    void Respond_RRQ() {
        char buf[BUFFER_SIZE];
        memmove(buf, &DATA, 2);
        unsigned short blockNumber = position % UINT16_MAX;
        memmove(buf + 2, &blockNumber, 2);
        file.read(buf + 4, blockSize);
        int size = file.gcount();
        Send(buf, size + 4);
    }

    unsigned short GetType() {
        return *((unsigned short*)buf);
    }

    unsigned short GetBlock() {
        return *((unsigned short*)(buf + 2));
    }
    string GetString(int pos) {
        string result;
        while(buf[pos] != '\0') {
            result += buf[pos++];
        }
        return result;
    }
};

unordered_map<int, Client> connection;
struct epoll_event ev, events[MAX_EVENTS];
struct sockaddr_in connected;
socklen_t ssize = sizeof(connected);

int write_to_buf(char *buf, int start, string msg){
	for(int i = 0; i <(int) msg.size(); i++){
		buf[i + start] = msg[i];
	}
	buf[start + msg.size()] = 0;
	return start + msg.size() + 1;
}

int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(0);
    srand(time(0));

    struct sockaddr_in name;
    int listen_sock = socket(AF_INET, SOCK_DGRAM, 0);



    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(69);
    name.sin_addr.s_addr = 0;

    if(bind(listen_sock, (sockaddr*)&name, sizeof(name)) < 0){
        perror("bind");
        exit(EXIT_FAILURE);
    }

    int conn_sock, nfds, epollfd;

    epollfd = epoll_create1(0);
    if(epollfd == -1){
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    ev.events = EPOLLIN;
    ev.data.fd = listen_sock;

    if(epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev) == -1){
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }

    for(;;) { 
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if(nfds == -1){
            perror("epoll wait");
            exit(EXIT_FAILURE);
        }
        char buf[BUFFER_SIZE];
        for(int n = 0; n < nfds; n++) {
            if(events[n].data.fd == listen_sock) {
                int data = recvfrom(listen_sock, buf, BUFFER_SIZE, 0, (sockaddr*)&connected, &ssize);
                conn_sock = socket(AF_INET, SOCK_DGRAM, 0);
                struct sockaddr_in newSock;
                memset(&newSock, 0, sizeof(newSock));
                newSock.sin_addr.s_addr = 0;
                newSock.sin_port = 0;
                newSock.sin_family = AF_INET;
                if(bind(conn_sock, (sockaddr*)&newSock, sizeof(newSock)) < 0){
                    perror("bind: conn_sock");
                    close(conn_sock);
                    continue;
                }
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = conn_sock;
                if(epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock, &ev) < 0){
                    perror("epoll_ctl: conn_sock");
                    close(conn_sock);
                }
                connection[conn_sock] = Client(conn_sock, connected, buf, data);

                
                connection[conn_sock].Respond_RRQ();
            } else {
                int fd = events[n].data.fd;
                connection[fd].Respond_RRQ();
            }
        }
    }

    close(epollfd);
    return 0;
}
