/*
 * Copyright (c) 2016-2017 Andrea Giacomo Baldan
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include "networking.h"
#include "commands.h"
#include "util.h"


/* set non-blocking socket */
int set_nonblocking(int fd) {
    int flags, result;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        return -1;
    }
    result = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (result == -1) {
        perror("fcntl");
        return -1;
    }
    return 0;
}


// auxiliary function for creating epoll server
static int create_and_bind(const char *host, const char *port) {
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int sfd;

    memset(&hints, 0, sizeof (struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;     /* 0.0.0.0 all interfaces */

    if (getaddrinfo(host, port, &hints, &result) != 0) {
        perror("getaddrinfo error");
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (sfd == -1) continue;

        if ((bind(sfd, rp->ai_addr, rp->ai_addrlen)) == 0)
            /* We managed to bind successfully! */
            break;

        close(sfd);
    }

    if (rp == NULL) {
        perror("Could not bind");
        return -1;
    }

    freeaddrinfo(result);

    return sfd;
}


/*
 * Start listening on the specfied address and port
 */
int listento(const char *host, const char *port) {
    int sfd;

    if ((sfd = create_and_bind(host, port)) == -1)
        abort();

    if ((set_nonblocking(sfd)) == -1)
        abort();

    if ((listen(sfd, SOMAXCONN)) == -1) {
        perror("listen");
        abort();
    }

    if (host == NULL)
        LOG("Instance listening on 127.0.0.1:%s\n", port);
    else LOG("Instance listening on %s:%s\n", host, port);

    return sfd;
}


/*
 * Start an event loop waiting for incoming events on fd
 */
int event_loop(int *fds, size_t len, map *m, fd_handler handler_ptr) {

    /* Check the number of descriptor */
    if (len < 2) {
        fprintf(stderr, "Descriptor number must be at least 2\n");
        exit(EXIT_FAILURE);
    }

    struct epoll_event ev, evs[MAX_EVENTS];
    struct sockaddr addr;
    socklen_t addrlen = sizeof addr;
    int epollfd, nfds, client, done = 0;

    if ((epollfd = epoll_create1(0)) == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    for (int n = 0; n < len; ++n) {
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = fds[n];
        if(epoll_ctl(epollfd, EPOLL_CTL_ADD, fds[n], &ev) == -1) {
            perror("epoll_ctl");
            exit(EXIT_FAILURE);
        }
    }

    while(1) {
        if ((nfds = epoll_wait(epollfd, evs, MAX_EVENTS, -1)) == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < nfds; ++i) {
            /* If fdescriptor is main server or bus server*/
            if (evs[i].data.fd == fds[0] || evs[i].data.fd == fds[1]) {
                if ((client = accept(evs[i].data.fd,
                                (struct sockaddr *) &addr, &addrlen)) == -1) {
                    perror("accept");
                    exit(EXIT_FAILURE);
                }
                set_nonblocking(client);
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = client;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client, &ev) == -1) {
                    perror("epoll_ctl: client connection");
                    exit(EXIT_FAILURE);
                }
            } else {
                done = (*handler_ptr)(evs[i].data.fd, m);
                if (done) break;
            }
        }
        if (done) break;
    }
    return 0;
}

