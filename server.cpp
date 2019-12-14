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
#include <chrono>
#include <boost/lexical_cast.hpp>

#define MAX_EVENTS 1000U
#define MAX_TIMEOUTS 3U
#define DEFAULT_WSIZE 1U
#define DEFAULT_BSIZE 512U
#define DEFAULT_TIMEOUT 200
#define BUFFER_SIZE 10000U

const unsigned short RRQ = 1;
const unsigned short WRQ = 2;
const unsigned short DATA = 3;
const unsigned short ACK = 4;
const unsigned short ERROR = 5;
const unsigned short OACK = 6;
const unsigned short MAX_BLOCK_SIZE = 65464;
const unsigned short MIN_BLOCK_SIZE = 8;
const unsigned short MAX_WINDOW_SIZE = 65535;
const unsigned short MIN_WINDOW_SIZE = 1;

using namespace std;

using Clock = std::chrono::high_resolution_clock;
using Ms = std::chrono::milliseconds;

class Client {
    private:
    int fd, length = 0;
    unsigned long long position = 0, last_position = 0;
    unsigned short blockSize = DEFAULT_BSIZE, windowSize = DEFAULT_WSIZE, type, last_ACK = 0, last_block = 0,
                    timeouts = 0;
    struct sockaddr_in address;
    socklen_t ssize = sizeof(struct sockaddr_in);
    char buf[BUFFER_SIZE];
    string fileName;
    fstream file;
    bool end = false;
    Clock::time_point t_end = Clock::now() + Ms(DEFAULT_TIMEOUT);

    friend void swap(Client& first, Client& second){
        using std::swap;
        swap(first.fd, second.fd);
        swap(first.length, second.length);
        swap(first.position, second.position);
        swap(first.last_position, second.last_position);
        swap(first.blockSize, second.blockSize);
        swap(first.windowSize, second.windowSize);
        swap(first.type, second.type);
        swap(first.last_ACK, second.last_ACK);
        swap(first.last_block, second.last_block);
        swap(first.timeouts, second.timeouts);
        swap(first.address, second.address);
        swap(first.ssize, second.ssize);
        swap(first.buf, second.buf);
        swap(first.fileName, second.fileName);
        swap(first.file, second.file);
        swap(first.end, second.end);
        swap(first.t_end, second.t_end);
    }

    public:

    Client() {}

    Client(int fd, struct sockaddr_in& addr, char buf[], int length){
        memmove(this->buf, buf, length);
        memmove(&address, &addr, sizeof(addr));
        this->length = length;
        fileName = GetString(2);
        this->fd = fd;
        type = GetType();
        if(type == RRQ){
            file.open(fileName, ios::in | ios::binary);
        } else {
            file.open(fileName, ios::app | ios::binary);
        }
    }

    Client(const Client& other) {
        memmove(buf, other.buf, other.length);
        memmove(&address, &(other.address), sizeof(other.address));
        fd = other.fd;
        length = other.length;
        position = other.position;
        last_position = other.last_position;
        blockSize = other.blockSize;
        windowSize = other.windowSize;
        type = other.type;
        last_ACK = other.last_ACK;
        last_block = other.last_block;
        timeouts = other.timeouts;
        ssize = other.ssize;
        fileName = other.fileName;
        end = other.end;
        t_end = other.t_end;
        if(type == RRQ){
            file.open(fileName, ios::in | ios::binary);
        } else {
            file.open(fileName, ios::app | ios::binary);
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
        t_end = Clock::now() + Ms(DEFAULT_TIMEOUT);
        return sendto(fd, buf, bytes, 0, (sockaddr*)&address, ssize);
    }

    int Recv() {
        length = recvfrom(fd, buf, BUFFER_SIZE, 0, (sockaddr*)&address, &ssize);
        timeouts = 0;
        Respond();
        return length;
    }

    void Respond() {
        switch (GetType()) {
            case RRQ:
                if(!GetOptions()) Respond_RRQ();
                break;
            case ACK: {
                unsigned short blocknum = *((unsigned short*)(buf + 2));
                if(!is_good_ack(blocknum)) break;
                correct_position(blocknum);
                last_ACK = blocknum;
                if((*this)) {
                    switch (type) {
                        case RRQ:
                            Respond_RRQ();
                            break;
                    }
                }
                break;
            }
            case ERROR:
                end = true;
                break;
        }
    }

    void Respond_RRQ() {
        last_position = position;
        char buf[BUFFER_SIZE];
        for(int i = 0; i < windowSize; i++) {
            memmove(buf, &DATA, 2);
            unsigned short blockNumber = position / blockSize + 1;
            cerr << "SENDING " << blockNumber << "\n";
            memmove(buf + 2, &blockNumber, 2);
            file.read(buf + 4, blockSize);
            int size = file.gcount();
            position += blockSize;
            Send(buf, size + 4);
            if(size < blockSize) {
                last_block = blockNumber;
                end = true;
                break;
            }
        }
    }

    void Retransmit() {
        end = false;
        timeouts++;
        if(position == 0) {
            t_end = Clock::now() + Ms(DEFAULT_TIMEOUT);
            return;
        }
        switch (type) {
            case RRQ:
                position = last_position;
                file.clear();
                file.seekg(position, ios_base::beg);
                cerr << "RETRANSMISSION\n";
                Respond_RRQ();
        }
    }

    unsigned short GetType() {
        return *((unsigned short*)buf);
    }

    auto GetEndTime() {
        return t_end;
    }

    string GetString(int pos) {
        string result;
        while(buf[pos] != '\0') {
            result += buf[pos++];
        }
        return result;
    }

    bool GetOptions() {
        int k = 2;
        int zeros = 0;
        while(zeros < 2){
            if(buf[k++] == 0) zeros++;
        }
        if(k == length) return false;

        char ans[BUFFER_SIZE];
        memmove(ans, &OACK, 2);
        int pos = 2;
        string opt;
        while(k < length) {
            if(buf[k] != 0) {
                opt += tolower(buf[k++]);
            } else {
                string svalue;
                while(buf[++k] != 0) svalue += buf[k];
                unsigned short value = boost::lexical_cast<unsigned short>(svalue);
                if(opt == "blksize") {
                    if(value < MIN_BLOCK_SIZE || value > MAX_BLOCK_SIZE) value = DEFAULT_BSIZE;
                    blockSize = value;
                    pos = write_to_buf(ans, pos, opt);
                    pos = write_to_buf(ans, pos, boost::lexical_cast<string>(value));
                } else if(opt == "windowsize") {
                    if(value < MIN_WINDOW_SIZE || value > MAX_WINDOW_SIZE) value = DEFAULT_WSIZE;
                    windowSize = value;
                    pos = write_to_buf(ans, pos, opt);
                    pos = write_to_buf(ans, pos, boost::lexical_cast<string>(value));
                }
                opt.clear();
                k++;
            }
        }
        cerr << windowSize << " " << blockSize << "\n";
        Send(ans, pos);
        return true;
    }

    int write_to_buf(char *buf, int start, string msg) {
	    for(int i = 0; i <(int) msg.size(); i++) {
		    buf[i + start] = msg[i];
        }
	    buf[start + msg.size()] = 0;
	    return start + msg.size() + 1;
    }

    bool is_good_ack(unsigned short ack) {
        if(position == 0 && ack == 0) return true;
        unsigned short last_sent = (position / blockSize + 1);
        if(last_sent < last_ACK) {
            return ack > last_ACK || ack <= last_sent;
        }
        return ack > last_ACK && ack <= last_sent;
    }

    void correct_position(unsigned short block) {
        unsigned short last_block = position / blockSize;
        if(block == last_block) return;
        unsigned long long diff = 0;
        if(last_block > block) {
            diff = last_block - block;
        } else {
            diff = last_block + 1 + (UINT16_MAX - block);
        }
        position -= diff * blockSize;
        file.clear();
        file.seekg(position, ios_base::beg);
    }

    operator bool() const {
        return timeouts < MAX_TIMEOUTS && !(end && last_ACK == last_block);
    }
};

unordered_map<int, Client> connection;
struct epoll_event ev, events[MAX_EVENTS];
struct sockaddr_in connected;
socklen_t ssize = sizeof(connected);

int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(0);
    srand(time(0));

    struct sockaddr_in name;
    int listen_sock = socket(AF_INET, SOCK_DGRAM, 0);



    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(69);
    name.sin_addr.s_addr = 0;

    if(bind(listen_sock, (sockaddr*)&name, sizeof(name)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    int conn_sock, nfds, epollfd;

    epollfd = epoll_create1(0);
    if(epollfd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    ev.events = EPOLLIN;
    ev.data.fd = listen_sock;

    if(epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev) == -1) {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }

    for(;;) { 
        int timeout = DEFAULT_TIMEOUT + 1;
        auto start = Clock::now();
        for(auto& it : connection) {
            int diff = chrono::duration<double, milli>(it.second.GetEndTime() - start).count();
            timeout = min(timeout, diff);
            timeout = max(0, timeout);
        }
        if(timeout > DEFAULT_TIMEOUT) timeout = -1;
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, timeout);
        if(nfds == -1) {
            perror("epoll wait");
            exit(EXIT_FAILURE);
        }
        if(nfds == 0) {
            auto it = connection.begin();
            while(it != connection.end()) {
                int diff = chrono::duration<double, milli>(it->second.GetEndTime() - start).count();
                if(diff <= 0) {
                    if(!(it->second)) {
                        cerr << "connection closed\n";
                        close(it->first);
                        it = connection.erase(it);
                        continue;
                    }
                    it->second.Retransmit();
                }
                ++it;
            }
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
                if(bind(conn_sock, (sockaddr*)&newSock, sizeof(newSock)) < 0) {
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
                connection[conn_sock].Respond();

            } else {
                int fd = events[n].data.fd;
                connection[fd].Recv();
                if(!connection[fd]){
                    cerr << "connection closed\n";
                    close(fd);
                    connection.erase(fd);
                }
            }
        }
    }

    close(epollfd);
    return EXIT_SUCCESS;
}