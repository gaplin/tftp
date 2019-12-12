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

int main(int argc, char** argv) {
    std::ios_base::sync_with_stdio(0);
    srand(time(0));

    std::string port = "69";
    std::string ip = "azure.prajer.ninja";

    int sock_fd, s;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
    hints.ai_flags = 0;
    hints.ai_protocol = 0;          /* Any protocol */

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
                   break;                  /* Success */

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
    std::string file = "pxelinux.2";
    std::string mode = "octet";
    char msg[100000];
    char buf[10000];
    msg[0] = 0;
    msg[1] = 1;
    ofstream outFile(file.c_str());
    int pos = write_to_buf(msg, 2, file);
    pos = write_to_buf(msg, pos, mode);

    sendto(sock_fd, msg, pos, 0, (sockaddr*)&name, ssize);
    name.sin_port = 0;
    std::cerr << "msg sent\n";
    while(true){
	    int k = recvfrom(sock_fd, buf, 10000, 0, (sockaddr*)&name, &ssize);
	    if(buf[1] == (char)5){
		    cerr << "error ";
		    unlink(file.c_str());
		    for(int i = 4; i < k; i++){
			    cerr << buf[i];
		    }
		    break;
	    }
	    for(int i = 4; i < k; i++){
		    outFile << buf[i];
	    }
	    msg[0] = 0;
	    msg[1] = 4;
	    msg[2] = buf[2];
	    msg[3] = buf[3];
	    sendto(sock_fd, msg, 4, 0, (sockaddr*)&name, ssize);
	    if(k < 516)
		    break;
    }

    close(sock_fd);

    return 0;
}
