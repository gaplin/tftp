// vim:ts=4:sts=4:sw=4:expandtab

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <boost/program_options.hpp>

#include <gsl/gsl_errno.h>
#include <gsl/gsl_fft_complex.h>

#include <complex>
#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <ctime>
#include <fstream>

using namespace std;
namespace po = boost::program_options;


#define BUFFER_SIZE 10000U
#define DEFAULT_WSIZE 1U
#define DEFAULT_BSIZE 512U

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

struct addrinfo hints;
struct addrinfo *result, *rp;

socklen_t ssize = sizeof(sockaddr_in);

int write_to_buf(char *buf, int start, string msg){
	for(int i = 0; i <(int) msg.size(); i++){
		buf[i + start] = msg[i];
	}
	buf[start + msg.size()] = 0;
	return start + msg.size() + 1;
}

void getOptions(unsigned short& blocksize, unsigned short& windowsize, char buf[], int size) {
    string opt;
    int k = 2;
    while(k < size) {
        if(buf[k] != 0) {
            opt += tolower(buf[k++]);
        } else {
            if(opt == "blksize") blocksize = *((unsigned short*)(buf + k + 1));
            else windowsize = *((unsigned short*)(buf + k + 1));
            opt.clear();
            k += 3;
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
    ("write,w", po::value<string>(), "Write")
    ("read,r", po::value<string>(), "Read")
    ("blocksize", po::value<unsigned short>(), "Blocksize")
    ("windowsize", po::value<unsigned short>(), "Windowsize");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if(vm.count("help")) {
        cout << desc << "\n";
        return 1;
    }

    string port = "69";
    string ip = "azure.prajer.ninja";
    string file = "pxelinux.2";
    unsigned short windowsize = DEFAULT_WSIZE;
    unsigned short blocksize = DEFAULT_BSIZE;
    unsigned short request = RRQ;
    unsigned short requested_windowsize = DEFAULT_WSIZE;
    unsigned short requested_blocksize = DEFAULT_BSIZE;


    if(vm.count("host")) {
        ip = vm["host"].as<string>();
    }
    if(vm.count("read")) {
        request = RRQ;
        file = vm["read"].as<string>();
    }
    else if(vm.count("write")) {
        request = WRQ;
        file = vm["write"].as<string>();
    }
    else {
        cout << desc << "\n";
        return 1;
    }
    if(vm.count("blocksize")) {
        requested_blocksize = vm["blocksize"].as<unsigned short>();
    }
    if(vm.count("windowsize")) {
        requested_windowsize = vm["windowsize"].as<unsigned short>();
    }

    int sock_fd, s;

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

    struct sockaddr_in name;
    memset(&name, 0, sizeof(name));
    if(rp->ai_addr->sa_family == AF_INET){
        memmove(&name, rp->ai_addr, sizeof(struct sockaddr_in));
    }

    close(sock_fd);


    name.sin_port = htons(69);
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_sec = 20000;
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));


    string mode = "octet";


    char msg[BUFFER_SIZE];
    char buf[BUFFER_SIZE];
    memmove(msg, &request, 2);
    ofstream outFile(file.c_str());
    int pos = write_to_buf(msg, 2, file);
    pos = write_to_buf(msg, pos, mode);
    if(requested_blocksize != DEFAULT_BSIZE) {
        pos = write_to_buf(msg, pos, "blksize");
        memmove(msg + pos, &requested_blocksize, 2);
        pos += 2;
    }
    if(requested_windowsize != DEFAULT_WSIZE) {
        pos = write_to_buf(msg, pos, "windowsize");
        memmove(msg + pos, &requested_windowsize, 2);
        pos += 2;
    }

    sendto(sock_fd, msg, pos, 0, (sockaddr*)&name, ssize);
    name.sin_port = 0;
    std::cerr << "msg sent\n";
    int block = 0;
    while(true){
	    int k = recvfrom(sock_fd, buf, BUFFER_SIZE, 0, (sockaddr*)&name, &ssize);
	    if(*((unsigned short*)buf) == ERROR) {
		    cerr << "error ";
		    unlink(file.c_str());
		    for(int i = 4; i < k; i++){
			    cerr << buf[i];
		    }
		    break;
	    }
        else if(*((unsigned short*)buf) == OACK) {
            getOptions(blocksize, windowsize, buf, k);
            memmove(msg, &ACK, 2);
            msg[2] = msg[3] = 0;
            sendto(sock_fd, msg, 4, 0, (sockaddr*)&name, ssize);
            continue;
        }
        block++;
	    for(int i = 4; i < k; i++){
		    outFile << buf[i];
	    }
        if(block == windowsize || k < 4 + blocksize) {
            memmove(msg, &ACK, 2);
            memmove(msg + 2, buf + 2, 2);
	        sendto(sock_fd, msg, 4, 0, (sockaddr*)&name, ssize);
            block = 0;
        }
	    if(k < 4 + blocksize)
		    break;
    }

    close(sock_fd);

    return 0;
}
