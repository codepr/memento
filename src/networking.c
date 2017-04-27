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
#include <arpa/inet.h>
#include "networking.h"
#include "serializer.h"
#include "commands.h"
#include "cluster.h"
#include "queue.h"
#include "util.h"



int send_all(int sfd, char *buf, int *len) {

    int total = 0;
    int bytesleft = *len;
    int n;

    while (total < *len) {
        n = send(sfd, buf + total, bytesleft, 0);
        if (n == -1) break;
        total += n;
        bytesleft -= n;
    }

    *len = total;

    return n == -1 ? -1 : 0;
}



/* Set non-blocking socket */
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


/* Auxiliary function for creating epoll server */
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

        /* set SO_REUSEADDR so the socket will be reusable after process kill */
        if (setsockopt(sfd, SOL_SOCKET, (SO_REUSEPORT | SO_REUSEADDR),
                    &(int) { 1 }, sizeof(int)) < 0)
            perror("SO_REUSEADDR");

        if ((bind(sfd, rp->ai_addr, rp->ai_addrlen)) == 0) {
            /* Succesful bind */
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
 * Create a non-blocking socket and make it listen on the specfied address and
 * port
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
 * Create a socket and use it to connect to the specified host and port
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


static int epoll_fd;


static void *worker(void *args) {

    int done = 0;
    struct epoll_event *events = calloc(MAX_EVENTS, sizeof(*events));
    if (events == NULL) {
        perror("calloc(3) failed when attempting to allocate events buffer");
        pthread_exit(NULL);
    }

    int events_cnt;
    userdata_t *udata = NULL;
    while ((events_cnt = epoll_wait(epoll_fd, events, MAX_EVENTS, -1)) > 0) {
        for (int i = 0; i < events_cnt; i++) {

            //assert(events[i].events & EPOLLIN);

            if (events[i].events & EPOLLIN) {
                /* There's some data to be processed */
                if (events[i].data.fd < 0)
                    continue;
                done = command_handler(events[i].data.fd, 0);
                if (done == END || done == 1) {
                    /* close the connection */
                    LOG(DEBUG, "Closing connection\n");
                    close(events[i].data.fd);
                }
            } else if (events[i].events & EPOLLOUT) {
                udata = (userdata_t *) events[i].data.ptr;

                if (send_all(udata->fd, udata->data, (int *) &udata->size) < 0)
                    perror("Send data failed");

                struct epoll_event epevent;
                epevent.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                epevent.data.fd = udata->fd;

                if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, udata->fd, &epevent) < 0) {
                    perror("epoll_ctl(2) failed attempting to mod client");
                    close(udata->fd);
                }

                /* Check if struct udata contains allocated memory */
                if (udata->alloc == 1) free(udata->data);
                free(udata);
            }
        }
    }

    if (events_cnt == 0) {
        fprintf(stderr, "epoll_wait(2) returned 0, but timeout was not specified...?");
    } else {
        perror("epoll_wait(2) error");
    }

    free(events);

    return NULL;
}


/*
 * Main event loop thread, awaits for incoming connections using the global
 * epoll instance, his main responsibility is to pass incoming client
 * connections descriptor to a worker thread according to a simple round robin
 * scheduling, other than this, it is the sole responsible of the communication
 * between nodes if the system is started in cluster mode.
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

    /* Number of worker and writer threads to create, should be tweaked basing
       on the number of the physical cores provided by the CPU */
    int poolnr = 20;
    /* worker thread pool */
    pthread_t workers[poolnr];
    userdata_t *udata = NULL;

    if ((epoll_fd = epoll_create(1)) < 0) {
        perror("epoll_create(2) failed");
        exit(EXIT_FAILURE);
    }

    /* I/0 thread pool initialization, allocating a worker_epool structure for
       each one. A worker_epool structure is formed of an epoll descriptor and
       his event queue. Every  worker_epoll is added to a list, in order to
       reuse them in the event loop to add connecting descriptors in a round
       robin scheduling */
    for (int i = 0; i < poolnr; ++i)
        pthread_create(&workers[i], NULL, worker, NULL);

    int nfds;

    /* Add two already listening nonblocking descriptors to the loop, the first
       one represent the main point of access for clients, the second one is
       responsible for the communication between nodes (bus) */
    for (int n = 0; n < len; ++n) {
        instance.ev.data.fd = fds[n];
        instance.ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
        if(epoll_ctl(instance.epollfd,
                    EPOLL_CTL_ADD, fds[n], &instance.ev) == -1) {
            perror("epoll_ctl");
            exit(EXIT_FAILURE);
        }
    }

    /* Start the main event loop, epoll_wait blocks untill an event occur */
    while (1) {

        /* Reset the cycle of the round-robin selection of epoll fds */

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
                close(instance.evs[i].data.fd);
                continue;
            }
            /* If fdescriptor is main server or bus server add it to worker
               threads or to the global epoll instance respectively */
            if (instance.evs[i].data.fd == fds[0]
                    || instance.evs[i].data.fd == fds[1]) {

                if ((client = accept(instance.evs[i].data.fd,
                                (struct sockaddr *) &addr, &addrlen)) == -1) {
                    perror("accept");
                    close(instance.evs[i].data.fd);
                }

                set_nonblocking(client);

                /* Client connection check, this case must add the descriptor
                   to the next worker thread in the list */
                if (instance.evs[i].data.fd == fds[0]) {

                    struct epoll_event epevent;
                    epevent.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                    epevent.data.fd = client;

                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client, &epevent) < 0) {
                        perror("epoll_ctl(2) failed attempting to add new client");
                        close(client);
                        return -1;
                    }

                    struct epoll_event epevent2;
                    epevent2.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                    epevent2.data.fd = fds[0];

                    if (epoll_ctl(instance.epollfd, EPOLL_CTL_MOD, fds[0], &epevent2) < 0) {
                        perror("epoll_ctl(2) failed attempting to add new client");
                        close(fds[0]);
                        return -1;
                    }

                    LOG(DEBUG, "Client connected\r\n");
                }

                /* Bus connection check, in this case, the descriptor
                   represents another node connecting, must be added to the
                   global epoll instance */
                if (instance.evs[i].data.fd == fds[1]) {

                    instance.ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                    instance.ev.data.fd = client;

                    if (epoll_ctl(instance.epollfd,
                                EPOLL_CTL_ADD, client, &instance.ev) == -1) {
                        perror("epoll_ctl: client connection");
                    }


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
                    } else if (cluster_reachable(new_node) == 0) {
                        /* set the node already present to state REACHABLE */
                        if (cluster_set_state(new_node, REACHABLE) == 0)
                            LOG(DEBUG, "Failed to set node located at %s:%s to state REACHABLE",
                                    hbuf, sbuf);
                        else LOG(DEBUG, "Node %s:%s is now REACHABLE\n", hbuf, sbuf);
                    } else free(new_node); // the node is already present and REACHABLE

                }
            } else if (instance.evs[i].events & EPOLLIN) {

                /* There's some data from peer nodes to be processed, check if
                   the lock is released (i.e. the cluster has succesfully
                   formerd) and handle incoming messages */

                int done = 0;
                if (instance.lock == 0) {
                    done = (*handler_ptr)(instance.evs[i].data.fd, 1);
                    if (done == END) {
                        /* close the connection */
                        LOG(DEBUG, "Closing connection request\n");
                        close(instance.evs[i].data.fd);
                        break;
                    }
                    //else {
                    //    struct epoll_event epevent2;
                    //    epevent2.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                    //    epevent2.data.fd = instance.evs[i].data.fd;

                    //    if (epoll_ctl(instance.epollfd, EPOLL_CTL_MOD, instance.evs[i].data.fd, &epevent2) < 0) {
                    //        perror("epoll_ctl(2) failed attempting to add new client");
                    //        close(fds[0]);
                    //        return -1;
                    //    }

                    //}
                }
            }
            else if (instance.evs[i].events & EPOLLOUT) {
                udata = (userdata_t *) instance.evs[i].data.ptr;

                if (send_all(udata->fd, udata->data, (int *) &udata->size) < 0)
                    perror("Send data failed");

                struct epoll_event epevent;
                epevent.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                epevent.data.fd = udata->fd;

                //if (udata->buscomm == 1) {
                    if (epoll_ctl(instance.epollfd, EPOLL_CTL_MOD, udata->fd, &epevent) < 0) {
                        perror("epoll_ctl(2) failed attempting to mod client");
                        close(udata->fd);
                    }
                //} else {
                //    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, udata->fd, &epevent) < 0) {
                //        perror("epoll_ctl(2) failed attempting to mod client");
                //        close(udata->fd);
                //    }
                //}

                /* Check if struct udata contains allocated memory */
                if (udata->alloc == 1) free(udata->data);
                free(udata);

            }
        }
    }
    return 0;
}


/*
 * Add userdata_t structure to the global.write queue, a writer worker will
 * handle it
 */
void schedule_write(int sfd, char *data, unsigned long datalen, unsigned int alloc) {
    userdata_t *udata = calloc(1, sizeof(userdata_t));
    udata->fd = sfd;
    udata->data = data;
    udata->size = datalen;
    udata->alloc = alloc;

    struct epoll_event epevent;
    epevent.events = EPOLLOUT | EPOLLET | EPOLLONESHOT;
    epevent.data.ptr = udata;
//    epevent.data.fd = sfd;

    if (cluster_fd_contained(sfd) && sfd != self.fd) {

//        if (epoll_ctl(instance.epollfd, EPOLL_CTL_MOD, sfd, &epevent) < 0)
//            perror("epoll_ctl(2) mod");

        if (send_all(sfd, data, (int *) &datalen) < 0)
            perror("Send data failed");

        struct epoll_event epevent2;
        epevent2.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
        epevent2.data.fd = sfd;

        if (epoll_ctl(instance.epollfd, EPOLL_CTL_MOD, sfd, &epevent2) < 0) {
            perror("epoll_ctl(2) failed attempting to add new client");
            close(sfd);
        }
        /* Check if struct udata contains allocated memory */
        if (alloc == 1) free(data);

    } else {
        if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, sfd, &epevent) < 0) {
            perror("epoll_ctl(2) failed attempting to mod client");
            close(sfd);
        }
    }
}
