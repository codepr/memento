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

        if ((bind(sfd, rp->ai_addr, rp->ai_addrlen)) == 0) {
            /* set SO_REUSEADDR so the socket will be reusable after process kill */
            if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0)
                perror("SO_REUSEADDR");
            /* We managed to bind successfully! */
            break;
        }

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


pthread_mutex_t r_mutex;
/* pthread_mutex_t w_mutex; */
/* pthread_cond_t r_cond; */
/* pthread_cond_t w_cond; */


queue *connections_queue = NULL;
queue *outqueue = NULL;

struct worker_epoll {
    int efd;
    struct epoll_event event;
    struct epoll_event *events;
};


/* static void *connector(void *args) { */
/*  */
/*     struct worker_epoll *wep = (struct worker_epoll *) args; */
/*  */
/*     while(1) { */
/*  */
/*         #<{(| get descriptor if any |)}># */
/*         int descriptor = *(int *) dequeue(connections_queue); */
/*  */
/*  */
/*         wep->event.events = EPOLLIN | EPOLLET; */
/*         wep->event.data.fd = descriptor; */
/*  */
/*         if (epoll_ctl(wep->efd, EPOLL_CTL_ADD, descriptor, &wep->event) == -1) { */
/*             perror("epoll_ctl: client connection"); */
/*         } */
/*     } */
/*     return NULL; */
/* } */


static void *worker(void *args) {

    struct worker_epoll *wep = (struct worker_epoll *) args;
    int nfds, done;

    while (1) {

        if ((nfds = epoll_wait(wep->efd, wep->events, MAX_EVENTS, -1)) == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < nfds; ++i) {

            if (wep->events[i].events & EPOLLIN) {
                /*
                 * There's some data to be processed wait unitil lock is
                 * released
                 */
                if (wep->events[i].data.fd < 0)
                    continue;

                if (instance.cluster_mode == 1) {
                    /* check if the fd is contained in the cluster members */
                    if (cluster_fd_contained(wep->events[i].data.fd) == 1) {
                        /* it's a message from a peer node */
                        done = command_handler(wep->events[i].data.fd, 1);
                    } else {
                        /* it's a message from a connected client */
                        done = command_handler(wep->events[i].data.fd, 0);
                    }

                } else done = command_handler(wep->events[i].data.fd, 0);
                if (done == END || done == 1) {
                    /* close the connection */
                    LOG(DEBUG, "Closing connection\n");
                    close(wep->events[i].data.fd);
                }
            }
        }
    }
    return NULL;
}


static void *writer(void *args) {

    userdata_t *udata = NULL;

    while(1) {

        udata = (userdata_t *) dequeue(outqueue);

        if (send(udata->fd, udata->data, udata->size, 0) < 0)
            perror("Send data failed");

        free(udata);

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

    int client;
    struct sockaddr addr;
    socklen_t addrlen = sizeof addr;
    list *ep_list = list_create();

    connections_queue = create_queue();
    outqueue = create_queue();

    int poolnr = 4;
    // worker thread pool
    pthread_t workers[poolnr];
    pthread_t writers[poolnr];

    // I/0 thread pool start
    for (int i = 0; i < poolnr; ++i) {
        struct epoll_event event;
        struct epoll_event *events = calloc(MAX_EVENTS, sizeof(*events));
        int efd = epoll_create1(0);
        event.events = EPOLLIN | EPOLLET;
        struct worker_epoll *wp = calloc(1, sizeof(*wp));
        wp->efd = efd;
        wp->event = event;
        wp->events = events;
        list_head_insert(ep_list, wp);
        pthread_create(&writers[i], NULL, writer, NULL);
        pthread_create(&workers[i], NULL, worker, wp);
    }

    int nfds;

    for (int n = 0; n < len; ++n) {
        instance.ev.data.fd = fds[n];
        instance.ev.events = EPOLLIN | EPOLLET;
        if(epoll_ctl(instance.epollfd,
                    EPOLL_CTL_ADD, fds[n], &instance.ev) == -1) {
            perror("epoll_ctl");
            exit(EXIT_FAILURE);
        }
    }

    list_node *cursor = ep_list->head;
    struct worker_epoll *wep = NULL;

    while(1) {

        if (cursor == NULL) cursor = ep_list->head;

        if ((nfds = epoll_wait(instance.epollfd,
                        instance.evs, MAX_EVENTS, -1)) == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < nfds; ++i) {

            if ((instance.evs[i].events & EPOLLERR) ||
                    (instance.evs[i].events & EPOLLHUP)) {
                /* An error has occured on this fd, or the socket is not
                   ready for reading */
                perror ("epoll error");
                close (instance.evs[i].data.fd);
                continue;
            }
            /* If fdescriptor is main server or bus server*/
            if (instance.evs[i].data.fd == fds[0]
                    || instance.evs[i].data.fd == fds[1]) {


                if ((client = accept(instance.evs[i].data.fd,
                                (struct sockaddr *) &addr, &addrlen)) == -1) {
                    perror("accept");
                    exit(EXIT_FAILURE);
                }

                set_nonblocking(client);

                /* enqueue(connections_queue, &client); */

                wep = (struct worker_epoll *) cursor->data;

                wep->event.events = EPOLLIN | EPOLLET;
                wep->event.data.fd = client;

                if (epoll_ctl(wep->efd, EPOLL_CTL_ADD, client, &wep->event) == -1) {
                    perror("epoll_ctl: client connection");
                }

                cursor = cursor->next;

                if (instance.evs[i].data.fd == fds[1]) {
                    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
                    /* create a new cluster node */
                    cluster_node *new_node = shb_malloc(sizeof(cluster_node));
                    new_node->addr = hbuf;
                    new_node->port = GETINT(sbuf);
                    /* new_node->name = node_name(64); */

                    if (cluster_contained(new_node) == 0) {
                        new_node->fd = client;
                        new_node->state = REACHABLE;
                        new_node->self = 0;
                        /* add it to the cluster node list if not present*/
                        instance.ingoing =
                            list_head_insert(instance.ingoing, new_node);
                        /* instance.cluster = */
                        /*     list_head_insert(instance.cluster, new_node); */
                    } else if (cluster_reachable(new_node) == 0) {
                        /* set the node already present to state REACHABLE */
                        if (cluster_set_state(new_node, REACHABLE) == 0)
                            LOG(DEBUG, "[!] Failed to set node located at %s:%s to state REACHABLE",
                                    hbuf, sbuf);
                        else LOG(DEBUG, "Node %s:%s is now REACHABLE\n", hbuf, sbuf);
                    } else free(new_node); // the node is already present and REACHABLE

                }
                if (instance.evs[i].data.fd == fds[0])
                    LOG(DEBUG, "Client connected\r\n");

            } else if (instance.evs[i].events & EPOLLIN) {

                /* There's some data from peer nodes to be processed wait unitil
                   lock is released */

                int done = 0;
                if (instance.lock == 0) {
                    /* if cluster mode is enabled */
                    if (instance.cluster_mode == 1) {
                        /* check if the fd is contained in the cluster members */
                        if (cluster_fd_contained(instance.evs[i].data.fd) == 1) {
                            /* it's a message from a peer node */
                            done = (*handler_ptr)(instance.evs[i].data.fd, 1);
                        } else {
                            /* it's a message from a connected client */
                            done = (*handler_ptr)(instance.evs[i].data.fd, 0);
                            if (done == END) {
                                /* close the connection */
                                LOG(DEBUG, "Closing connection request\n");
                                close(instance.evs[i].data.fd);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}


void add_epollout_event(int sfd, char *data, unsigned long datalen, unsigned int from_peer) {
    /* if (send(sfd, data, datalen, 0) < 0) */
    /*     perror("Send data failed"); */
    userdata_t *udata = calloc(1, sizeof(*udata));
    udata->fd = sfd;
    udata->data = data;
    udata->size = datalen;
    udata->from_peer = from_peer;
    enqueue(outqueue, udata);
}
