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


#define MAX_EVENTS 1000

using namespace std;


struct epoll_event events[MAX_EVENTS];
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
    auto sock_fd = socket(AF_INET, SOCK_DGRAM, 0);

    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = 0;
    name.sin_addr.s_addr = 0;

    /*int epoll_fd = epoll_create1(0);
    if(epoll_fd == -1){
        cerr << "epoll error\n";
        return 1;
    }

    epoll_event event;
    event.data.fd = sock_fd;
    event.events = EPOLLIN;

    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &event) == -1){
        cerr << "epoll_ctl error\n";
        return 1;
    }

    //while(true){
      //  int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, 30000);
        //cerr << event_count << "\n";
    //}
    */
    char buf[5000];
    recvfrom(sock_fd, buf, 400, 0, (sockaddr*)&connected, &ssize);

    close(sock_fd);
    return 0;
}
