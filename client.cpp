#include <netdb.h>
#include <boost/program_options.hpp>
#include <boost/lexical_cast.hpp>
#include <iostream>
#include <fstream>

using namespace std;
namespace po = boost::program_options;


#define BUFFER_SIZE 10000U
#define DEFAULT_WSIZE 1U
#define DEFAULT_BSIZE 512U
#define MAX_TIMEOUTS 3
#define DEFAULT_PORT "69"
#define DEFAULT_IP "ip"

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

struct addrinfo hints;
struct addrinfo *result, *rp;
struct sockaddr_in name;

socklen_t ssize = sizeof(sockaddr_in);

fstream file;

int block = 0, k, sock_fd, s;
unsigned short windowsize = DEFAULT_WSIZE, blocksize = DEFAULT_BSIZE;
unsigned short last_block = 0, timeouts = 0, last_ACK = 0;
unsigned long long position = 0, last_position = 0;
char buf[BUFFER_SIZE];
bool endd = false;

int write_to_buf(char *buf, int start, string msg){
	for(int i = 0; i <(int) msg.size(); i++){
		buf[i + start] = msg[i];
	}
	buf[start + msg.size()] = 0;
	return start + msg.size() + 1;
}

bool is_good_ack(unsigned short ack) {
    if(position == 0 && ack == 0) return true;
    unsigned short last_sent = (position / blocksize);
    if(last_sent < last_ACK) {
        return ack > last_ACK || ack <= last_sent;
    }
    return ack > last_ACK && ack <= last_sent;
}

void correct_position(unsigned short block) {
    unsigned short last_blockNum = position / blocksize;
    if(block == last_blockNum) {
        return;
    }
    unsigned long long diff = 0;
    if(last_blockNum > block) {
        diff = last_blockNum - block;
    } else {
        diff = last_blockNum + 1 + (UINT16_MAX - block);
    }
    position -= diff * blocksize;
    file.clear();
    file.seekg(position, ios_base::beg);
}


void getOptions(unsigned short& blocksize, unsigned short& windowsize, char buf[], int size) {
    string opt;
    int k = 2;
    while(k < size) {
        if(buf[k] != 0) {
            opt += tolower(buf[k++]);
        } else {
            string svalue;
            while(buf[++k] != 0) svalue += buf[k];
            unsigned short value = boost::lexical_cast<unsigned short>(svalue);
            if(opt == "blksize") blocksize = value;
            else if(opt == "windowsize") windowsize = value;
            opt.clear();
            k++;
        }
    }
}

void sendACK(const int& blocknum) {
    cerr << "SENDING ACK " << blocknum << "\n";
    char buf[4];
    memmove(buf, &ACK, 2);
    memmove(buf + 2, &blocknum, 2);
    swap(buf[2], buf[3]);
    sendto(sock_fd, buf, 4, 0, (sockaddr*)&name, ssize);
}

void read_data() {
    block++;
    swap(buf[2], buf[3]);
    unsigned short blocknum = *((unsigned short*)(buf + 2));
    if(blocknum != (unsigned short)(last_block + 1)) {
        if(last_ACK != last_block) {
            sendACK(last_block);
            block = 0;
            last_ACK = last_block;
        }
        return;
    }
    for(int i = 4; i < k; i++){
        file << buf[i];
    }
    last_block = blocknum;
    if(block == windowsize || k < 4 + blocksize) {
        sendACK(blocknum);
        block = 0;
        last_ACK = blocknum;
    }
    if(k < 4 + blocksize) exit(EXIT_SUCCESS);
}

void sendData() {
    last_position = position;
    char buf[BUFFER_SIZE];
    for(int i = 0; i < windowsize; i++) {
        memmove(buf, &DATA, 2);
        unsigned short blockNumber = position / blocksize + 1;
        cerr << "SENDING DATA " << blockNumber << "\n";
        memmove(buf + 2, &blockNumber, 2);
        swap(buf[2], buf[3]);
        file.read(buf + 4, blocksize);
        int size = file.gcount();
        position += blocksize;
        sendto(sock_fd, buf, size + 4, 0, (sockaddr*)&name, ssize);
        if(size < blocksize) {
            last_block = blockNumber;
            endd = true;
            break;
        }
    }
}

int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(0);
    srand(time(0));

    po::options_description desc("Options");
    desc.add_options()
    ("help", "Print help message")
    ("host,h", po::value<string>(), "Host")
    ("port,p", po::value<string>(), "Port")
    ("write,w", po::value<string>(), "Write")
    ("read,r", po::value<string>(), "Read")
    ("blocksize", po::value<unsigned short>(), "Blocksize")
    ("windowsize", po::value<unsigned short>(), "Windowsize")
    ("output,o", po::value<string>(), "Output fileName");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if(vm.count("help")) {
        cout << desc << "\n";
        return 1;
    }

    string port = DEFAULT_PORT;
    string ip = DEFAULT_IP;
    string fileName, outFileName;
    unsigned short request = RRQ;
    unsigned short requested_windowsize = DEFAULT_WSIZE;
    unsigned short requested_blocksize = DEFAULT_BSIZE;


    if(vm.count("host")) {
        ip = vm["host"].as<string>();
    }
    if(vm.count("port")) {
        port = vm["port"].as<string>();
    }
    if(vm.count("read")) {
        request = RRQ;
        fileName = vm["read"].as<string>();
        outFileName = fileName;
    }
    else if(vm.count("write")) {
        request = WRQ;
        fileName = vm["write"].as<string>();
    }
    else {
        cout << desc << "\n";
        return 1;
    }
    if(vm.count("output")) {
        outFileName = vm["output"].as<string>();
    }
    if(vm.count("blocksize")) {
        requested_blocksize = vm["blocksize"].as<unsigned short>();
    }
    if(vm.count("windowsize")) {
        requested_windowsize = vm["windowsize"].as<unsigned short>();
    }


    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;   
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;        

    s = getaddrinfo(ip.c_str(), port.c_str(), &hints, &result);
           if (s != 0) {
               fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
               exit(EXIT_FAILURE);
           }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
               sock_fd = socket(rp->ai_family, rp->ai_socktype,
                            rp->ai_protocol);
               if (sock_fd == -1)
                   continue;

               if (connect(sock_fd, rp->ai_addr, rp->ai_addrlen) != -1)
                   break;

               close(sock_fd);
           }
     if(rp == NULL){
                std::cerr << "no addres found\n";
                exit(1);
        }

    memset(&name, 0, sizeof(name));
    if(rp->ai_addr->sa_family == AF_INET){
        memmove(&name, rp->ai_addr, sizeof(struct sockaddr_in));
    }

    close(sock_fd);


    name.sin_port = htons(boost::lexical_cast<int>(port));
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));


    string mode = "octet";


    char msg[BUFFER_SIZE];
    memmove(msg, &request, 2);

    if(request == RRQ){
        file.open(outFileName, ios::out | ios::binary);
    } else {
        int res = access(fileName.c_str(), R_OK);
        if(res < 0) {
            cerr << "ACCESS ERROR";
            exit(EXIT_FAILURE);
        }
        file.open(fileName, ios::in | ios::binary);
    }
    if(!file.good()) {
        cerr << "OPEN ERROR\n";
        exit(EXIT_FAILURE);
    }

    int pos = write_to_buf(msg, 2, fileName);
    pos = write_to_buf(msg, pos, mode);
    if(requested_blocksize != DEFAULT_BSIZE) {
        pos = write_to_buf(msg, pos, "blksize");
        pos = write_to_buf(msg, pos, boost::lexical_cast<string>(requested_blocksize));
    }
    if(requested_windowsize != DEFAULT_WSIZE) {
        pos = write_to_buf(msg, pos, "windowsize");
        pos = write_to_buf(msg, pos, boost::lexical_cast<string>(requested_windowsize));
    }

    sendto(sock_fd, msg, pos, 0, (sockaddr*)&name, ssize);
    name.sin_port = 0;
    cerr << "Init message sent\n";
    while(true){
	    k = recvfrom(sock_fd, buf, BUFFER_SIZE, 0, (sockaddr*)&name, &ssize);
        if(k <= 0) {
            if(request == RRQ) {
                sendACK(last_block);
                block = 0;
            }
            else {
                position = last_position;
                file.clear();
                file.seekg(position, ios_base::beg);
                sendData();
            }
            if(timeouts == MAX_TIMEOUTS) {
                cerr << "connection lost\n";
                close(sock_fd);
                exit(EXIT_FAILURE);
            }
            timeouts++;
            continue;
        }
        timeouts = 0;
	    if(*((unsigned short*)buf) == ERROR) {
		    cerr << "error ";
		    if(request == RRQ) unlink(outFileName.c_str());
		    for(int i = 4; i < k; i++){
			    cerr << buf[i];
		    }
		    break;
	    }
        else if(*((unsigned short*)buf) == OACK) {
            block = 0;
            getOptions(blocksize, windowsize, buf, k);
            if(request == RRQ) sendACK(0);
            else sendData();
            continue;
        }
        if(request == RRQ) read_data();
        else {
            swap(buf[2], buf[3]);
            unsigned short blocknum = *((unsigned short*)(buf + 2));
            cerr << "RECEIVED ACK " << blocknum << "\n";
            if(!is_good_ack(blocknum)) continue;
            correct_position(blocknum);
            last_ACK = blocknum;
            if(endd && last_ACK == last_block)exit(EXIT_SUCCESS);
            sendData();
        }
    }

    close(sock_fd);

    return EXIT_SUCCESS;
}
