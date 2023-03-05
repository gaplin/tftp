#include <sys/epoll.h>
#include <netdb.h>
#include <chrono>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <fstream>
#include <map>

#define MIN_TIMEOUT 1000
#define MAX_EVENTS 1000U
#define MAX_TIMEOUTS 3U
#define DEFAULT_WSIZE 1U
#define DEFAULT_BSIZE 512U
#define DEFAULT_TIMEOUT 2000
#define BUFFER_SIZE 100000U
#define DEFAULT_PORT 69
#define G 100

const unsigned short RRQ = (1 << 8);
const unsigned short WRQ = (2 << 8);
const unsigned short DATA = (3 << 8);
const unsigned short ACK = (4 << 8);
const unsigned short ERROR = (5 << 8);
const unsigned short OACK = (6 << 8);
const unsigned short MAX_BLOCK_SIZE = 65464;
const unsigned short MIN_BLOCK_SIZE = 8;
const unsigned short MAX_WINDOW_SIZE = 65535;
const unsigned short MIN_WINDOW_SIZE = 1;

using namespace std;
namespace po = boost::program_options;

using Clock = std::chrono::high_resolution_clock;
using Ms = std::chrono::milliseconds;
typedef pair<unsigned long, unsigned short> P;
bool onePort = false;


class Client {
    private:
    int fd, length = 0;
    unsigned long long position = 0, last_position = 0;
    unsigned short blockSize = DEFAULT_BSIZE, windowSize = DEFAULT_WSIZE, type, last_ACK = 0, last_block = 0,
                    timeouts = 0, curr_block = 0;
    struct sockaddr_in address;
    socklen_t ssize = sizeof(struct sockaddr_in);
    char buf[BUFFER_SIZE];
    string fileName;
    fstream file;
    bool end = false;
    Clock::time_point t_end = Clock::now() + Ms(DEFAULT_TIMEOUT);
    Clock::time_point start = Clock::now();
    double RTTm = -1, RTTs = -1, RTTd = -1, t = 0.125, k = 0.25;

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
        swap(first.curr_block, second.curr_block);
        swap(first.address, second.address);
        swap(first.ssize, second.ssize);
        swap(first.buf, second.buf);
        swap(first.fileName, second.fileName);
        swap(first.file, second.file);
        swap(first.end, second.end);
        swap(first.t_end, second.t_end);
        swap(first.start, second.start);
        swap(first.RTTm, second.RTTm);
        swap(first.RTTs, second.RTTs);
        swap(first.RTTd, second.RTTd);
        swap(first.t, second.t);
        swap(first.k, second.k);
    }

    void calc_timeout() {
        RTTm = chrono::duration<double, milli>(Clock::now() - start).count();
        RTTs = (RTTs == -1) ? RTTm : (1 - t) * RTTs + t * RTTm;
        RTTd = (RTTd == -1) ? RTTm / 2 : (1 - k) * RTTd + k * abs(RTTm - RTTs);
    }

    void update_timeout() {
        int timeout = (RTTm == -1) ? DEFAULT_TIMEOUT : (int)ceil(RTTs) + max(G, (int)ceil(4 * RTTd));
        if(timeout < MIN_TIMEOUT) timeout = MIN_TIMEOUT;
        t_end = Clock::now() + Ms(timeout);
    }

    void sendACK(const int& blocknum) {
        char buf[4];
        memmove(buf, &ACK, 2);
        memmove(buf + 2, &blocknum, 2);
        swap(buf[2], buf[3]);
        cerr << "SENDING ACK " << blocknum << "\n";
        Send(buf, 4);
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
            int res = access(fileName.c_str(), R_OK);
            if(res < 0) {
                if(errno == ENOENT) file.open(fileName, ios::out | ios::binary);
                return;
            }
            SendError(6, "File already exists");
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
        curr_block = other.curr_block;
        ssize = other.ssize;
        fileName = other.fileName;
        end = other.end;
        t_end = other.t_end;
        start = other.start;
        RTTm = other.RTTm;
        RTTs = other.RTTs;
        RTTd = other.RTTd;
        t = other.t;
        k = other.k;
        if(type == RRQ){
            file.open(fileName, ios::in | ios::binary);
        } else {
            int res = access(fileName.c_str(), R_OK);
            if(res < 0) {
                if(errno == ENOENT) file.open(fileName, ios::out | ios::binary);
                return;
            }
            SendError(6, "File already exists");
        }
    }

    Client(Client && other){
        swap(*this, other);
    }
    ~Client() {
        file.close();
        if(type == WRQ && !end) unlink(fileName.c_str());
    }

    Client& operator=(Client other) {
        swap(*this, other);
        return *this;
    }

    int Send(char buf[], int bytes) {
        update_timeout();
        start = Clock::now();
        return sendto(fd, buf, bytes, 0, (sockaddr*)&address, ssize);
    }

    int Recv(char buf[], int length) {
        this->length = length;
        memmove(this->buf, buf, length);
        if(length > 0) {
            if(type == RRQ) calc_timeout();
            else update_timeout();
            timeouts = 0;
        }
        return length;
    }

    bool Respond() {
        switch (GetType()) {
            case RRQ:
                if(!file.good()) {
                    SendError(1, "File not found.");
                    break;
                }
                if(!GetOptions() && (*this)) Respond_RRQ();
                break;
            case WRQ:
                if(!GetOptions() && (*this)) sendACK(0);
                break;
            case ACK: {
                swap(buf[2], buf[3]);
                unsigned short blocknum = *((unsigned short*)(buf + 2));
                if(!is_good_ack(blocknum)) return false;
                correct_position(blocknum);
                last_ACK = blocknum;
                if((*this)) Respond_RRQ();
                break;
            }
            case DATA: {
                position = 1;
                Respond_WRQ();
                break;
            }
            case ERROR:
                timeouts = MAX_TIMEOUTS;
                return false;
        }
        return true;
    }

    void Respond_RRQ() {
        last_position = position;
        char buf[BUFFER_SIZE];
        for(int i = 0; i < windowSize; i++) {
            memmove(buf, &DATA, 2);
            unsigned short blockNumber = position / blockSize + 1;
            cerr << "SENDING DATA " << blockNumber << "\n";
            memmove(buf + 2, &blockNumber, 2);
            swap(buf[2], buf[3]);
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

    void Respond_WRQ() {
        curr_block++;
        swap(buf[2], buf[3]);
        unsigned short blocknum = *((unsigned short*)(buf + 2));
        cerr << "RECEIVED DATA " << blocknum << "\n";
        if(blocknum != (unsigned short)(last_block + 1)) {
            if(last_ACK != last_block) {
                sendACK(last_block);
                curr_block = 0;
                last_ACK = last_block;
            }
            return;
        }
        if(blocknum == (unsigned short)(last_ACK + 1)) {
            calc_timeout();
        }
        for(int i = 4; i < length; i++){
		    file << buf[i];
	    }
        last_block = blocknum;
        if(curr_block == windowSize || length < 4 + blockSize) {
            sendACK(blocknum);
            curr_block = 0;
            last_ACK = blocknum;
        }
        if(length < 4 + blockSize) {
            end = true;
        }
    }

    void Retransmit() {
        end = false;
        timeouts++;
        cerr << "RETRANSMISSION\n";
        if(position == 0) {
            update_timeout();
            return;
        }
        switch (type) {
            case RRQ:
                position = last_position;
                file.clear();
                file.seekg(position, ios_base::beg);
                Respond_RRQ();
                break;
            case WRQ:
                sendACK(last_block);
                curr_block = 0;
                break;
        }
    }

    unsigned short GetType() {
        return *((unsigned short*)buf);
    }

    int GetFd() {
        return fd;
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

    int SendError(unsigned short errcode, string errmsg) {
        char buf[BUFFER_SIZE];
        memmove(buf, &ERROR, 2);
        memmove(buf + 2, &errcode, 2);
        swap(buf[2], buf[3]);
        int pos = write_to_buf(buf, 4, errmsg);
        timeouts = MAX_TIMEOUTS;
        end = true;
        return Send(buf, pos);
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
                    if(value < MIN_BLOCK_SIZE) {
                        SendError(0, "wrong blocksize");
                        return false;
                    }
                    if(value > MAX_BLOCK_SIZE) value = DEFAULT_BSIZE;
                    blockSize = value;
                    pos = write_to_buf(ans, pos, opt);
                    pos = write_to_buf(ans, pos, boost::lexical_cast<string>(value));
                } else if(opt == "windowsize") {
                    if(value < MIN_WINDOW_SIZE) {
                        SendError(0, "wrong windowsize");
                        return false;
                    }
                    if(value > MAX_WINDOW_SIZE) value = DEFAULT_WSIZE;
                    windowSize = value;
                    pos = write_to_buf(ans, pos, opt);
                    pos = write_to_buf(ans, pos, boost::lexical_cast<string>(value));
                }
                opt.clear();
                k++;
            }
        }
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
        unsigned short last_sent = (position / blockSize);
        if(last_sent < last_ACK) {
            return ack > last_ACK || ack <= last_sent;
        }
        return ack > last_ACK && ack <= last_sent;
    }

    void correct_position(unsigned short block) {
        unsigned short last_blockNum = position / blockSize;
        if(block == last_blockNum) return;
        unsigned long long diff = 0;
        if(last_blockNum > block) {
            diff = last_blockNum - block;
        } else {
            diff = last_blockNum + 1 + (UINT16_MAX - block);
        }
        position -= diff * blockSize;
        file.clear();
        file.seekg(position, ios_base::beg);
    }

    operator bool() const {
        if(type == RRQ) return timeouts < MAX_TIMEOUTS && !(end && last_ACK == last_block);
        return timeouts < MAX_TIMEOUTS && !(end);
    }
};

map<P, Client> connection;
struct epoll_event ev, events[MAX_EVENTS];
struct sockaddr_in connected;
socklen_t ssize = sizeof(connected);

int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(0);
    srand(time(0));

    po::options_description desc("Options");
    desc.add_options()
    ("help", "Print help message")
    ("port,p", po::value<unsigned short>(), "Port")
    ("oneport,o", "All queries on one port");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    unsigned short port = DEFAULT_PORT;

    if(vm.count("help")) {
        cout << desc << "\n";
        return 1;
    }
    if(vm.count("port")) {
        port = vm["port"].as<unsigned short>();
    }
    if(vm.count("oneport")) {
        onePort = true;
    }

    struct sockaddr_in name;
    int listen_sock = socket(AF_INET, SOCK_DGRAM, 0);

    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(port);
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
        cerr << "Setting epoll_wait timeout " << timeout << "\n";
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
                        if(!onePort) close(it->second.GetFd());
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
                P cl(connected.sin_addr.s_addr, connected.sin_port);
                if(!onePort) {
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

                    ev.events = EPOLLIN;
                    ev.data.fd = conn_sock;
                    if(epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock, &ev) < 0){
                        perror("epoll_ctl: conn_sock");
                        close(conn_sock);
                    }
                    connection[cl] = Client(conn_sock, connected, buf, data);
                } else {
                    if(connection.count(cl) == 0) {
                        connection[cl] = Client(listen_sock, connected, buf, data);
                    } else {
                        connection[cl].Recv(buf, data);
                    }
                }
                if(connection[cl]) connection[cl].Respond();
                else {
                    cerr << "connection closed\n";
                    if(!onePort) close(connection[cl].GetFd());
                    connection.erase(cl);
                }
            } else {
                int fd = events[n].data.fd;
                int length = recvfrom(fd, buf, BUFFER_SIZE, MSG_DONTWAIT, (sockaddr*)&connected, &ssize);
                P cl(connected.sin_addr.s_addr, connected.sin_port);
                connection[cl].Recv(buf, length);
                connection[cl].Respond();
                if(!connection[cl]){
                    cerr << "connection closed\n";
                    close(fd);
                    connection.erase(cl);
                }
            }
        }
    }

    close(epollfd);
    return EXIT_SUCCESS;
}