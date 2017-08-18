#include <stdio.h>
#include <sys/epoll.h>
#include "event.h"


void add_epollin(int efd, int fd) {
    struct epoll_event ev;
    ev.data.fd = fd;
    ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;

    if (epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        perror("epoll_ctl(2): add epollin");
    }
}

void set_epollout(int efd, int fd, void *data) {
    struct epoll_event ev;
    ev.data.fd = fd;
    if (data)
        ev.data.ptr = data;
    ev.events = EPOLLOUT | EPOLLET | EPOLLONESHOT;

    if (epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev) < 0) {
        perror("epoll_ctl(2): set epollout");
    }
}

void set_epollin(int efd, int fd) {
    struct epoll_event ev;
    ev.data.fd = fd;
    ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;

    if (epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev) < 0) {
        perror("epoll_ctl(2): set epollin");
    }
}


