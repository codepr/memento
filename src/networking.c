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
#include <pthread.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
/* #include <sys/epoll.h> */
#include <arpa/inet.h>
#include "networking.h"
#include "serializer.h"
#include "commands.h"
#include "cluster.h"
#include "queue.h"
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
        LOG(DEBUG, "Instance listening on 127.0.0.1:%s\n", port);
    else LOG(DEBUG, "Instance listening on %s:%s\n", host, port);

    return sfd;
}


/*
 * Start a connection to the specified host and port
 */
int connectto(const char *host, const char *port) {

    int p = atoi(port);
    struct sockaddr_in serveraddr;
    struct hostent *server;

    /* socket: create the socket */
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) perror("ERROR opening socket");

    /* gethostbyname: get the server's DNS entry */
    server = gethostbyname(host);
    if (server == NULL) {
        fprintf(stderr, "ERROR, no such host as %s\n", host);
        exit(EXIT_FAILURE);
    }

    /* build the server's address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *) server->h_addr,
            (char *) &serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(p);

    /* connect: create a connection with the server */
    if (connect(sfd, (const struct sockaddr *) &serveraddr,
                sizeof(serveraddr)) < 0) {

        LOG(ERR, "Connection error: %s\n", strerror(errno));
        return -1;
    }

    return sfd;
}


queue *task_queue = NULL;

static void *readtask(void *args) {
    int fd = -1;
    while(1) {
        struct task* tmp = (task_t *) dequeue(task_queue);
        fd = tmp->data.fd;
        free(tmp);
        command_handler(fd, 0);
    }

    return NULL;
}


/*
 * Start an event loop waiting for incoming events on fd
 */
int event_loop(int *fds, size_t len, fd_handler handler_ptr) {

    /* Check the number of descriptor */
    if (len < 2) {
        fprintf(stderr, "Descriptor number must be at least 2\n");
        exit(EXIT_FAILURE);
    }

    task_t *new_task = NULL;
    task_queue = create_queue();

    // read thread pool
    pthread_t read_pool[4];

    // threads for reading thread pool
    for (int i = 0; i < 4; ++i)
        pthread_create(&read_pool[i], NULL, readtask, NULL);

    struct sockaddr addr;
    socklen_t addrlen = sizeof addr;
    int nfds, client, done = 0;

    for (int n = 0; n < len; ++n) {
        instance.ev.data.fd = fds[n];
        if(epoll_ctl(instance.epollfd, EPOLL_CTL_ADD, fds[n], &instance.ev) == -1) {
            perror("epoll_ctl");
            exit(EXIT_FAILURE);
        }
    }

    while(1) {

        if ((nfds = epoll_wait(instance.epollfd, instance.evs, MAX_EVENTS, -1)) == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < nfds; ++i) {
            /* If fdescriptor is main server or bus server*/
            if (instance.evs[i].data.fd == fds[0]
                    || instance.evs[i].data.fd == fds[1]) {

                /* connection request incoming */

                if ((client = accept(instance.evs[i].data.fd,
                                (struct sockaddr *) &addr, &addrlen)) == -1) {
                    perror("accept");
                    exit(EXIT_FAILURE);
                }

                set_nonblocking(client);
                instance.ev.events = EPOLLIN | EPOLLET;
                instance.ev.data.fd = client;
                if (epoll_ctl(instance.epollfd, EPOLL_CTL_ADD, client, &instance.ev) == -1) {
                    perror("epoll_ctl: client connection");
                    exit(EXIT_FAILURE);
                }

                if (instance.evs[i].data.fd == fds[0])
                    LOG(DEBUG, "Client connected\r\n");

            } else {
                /*
                 * There's some data to be processed wait unitil lock is
                 * released
                 */
                if (instance.lock == 0) {
                    new_task = malloc(sizeof(struct task));
                    new_task->data.fd = instance.evs[i].data.fd;
                    new_task->next = NULL;
                    enqueue(task_queue, new_task);
                }
            }
        }
    }
    return 0;
}

