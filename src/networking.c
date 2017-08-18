/*
 * Copyright (c) 2016-2017 Andrea Giacomo Baldan
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
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
#include "commands.h"
#include "cluster.h"
#include "event.h"


void init_event_loop(const char *host,
		const char *server_port, const char *cluster_port) {
	instance.el.host = host;
	instance.el.server_port = server_port;
	instance.el.cluster_port = cluster_port;
	instance.el.epoll_workers = EPOLL_WORKERS;
	instance.el.max_events = MAX_EVENTS;
	instance.el.bufsize = BUFSIZE;

    /* initialize global epollfd */
    if ((instance.el.epollfd = epoll_create1(0)) == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

	/* initialize global bus epollfd */
	if ((instance.el.bepollfd = epoll_create1(0)) == -1) {
		perror("bepollfd not created");
		exit(EXIT_FAILURE);
	}

	instance.el.server_handler = client_command_handler;
	instance.el.cluster_handler = peer_command_handler;
}


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
        DEBUG("Instance listening on 127.0.0.1:%s\n", port);
    else DEBUG("Instance listening on %s:%s\n", host, port);

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
        ERROR("Connection error: %s\n", strerror(errno));
        return -1;
    }
    return sfd;
}


static void *worker(void *args) {

    int done = 0;
    struct epoll_event *events = calloc(instance.el.max_events, sizeof(*events));
    if (events == NULL) {
        perror("calloc(3) failed when attempting to allocate events buffer");
        pthread_exit(NULL);
    }

    int events_cnt;
    while ((events_cnt =
				epoll_wait(instance.el.bepollfd, events, instance.el.max_events, -1)) > 0) {
        for (int i = 0; i < events_cnt; i++) {

			if ((events[i].events & EPOLLERR) ||
					(events[i].events & EPOLLHUP)) {
				/* An error has occured on this fd */
				fprintf(stderr, "worker epoll error\n");
				close(events[i].data.fd);
				continue;
			}

            if (events[i].events & EPOLLIN) {
                /* There's some data to be processed */
                if (events[i].data.fd < 0)
                    continue;
                done = (instance.el.server_handler)(events[i].data.fd);
                if (done == END || done == 1) {
                    /* close the connection */
                    DEBUG("Closing connection\n");
                    close(events[i].data.fd);
					// TODO: remove fd from epoll
                }
            } else if (events[i].events & EPOLLOUT) {

				peer_t *p = (peer_t *) events[i].data.ptr;
				DEBUG("Answering to client from worker %d\n", p->fd);
                SET_FD_IN(instance.el.bepollfd, p->fd);

                if (send_all(p->fd, p->data, (int *) &p->size) < 0)
                    perror("Send data failed");

                /* Check if struct udata contains allocated memory */
                if (p->alloc == 1) free(p->data);
                free(p);
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
int start_loop(void) {

    struct epoll_event *events = calloc(instance.el.max_events, sizeof(*events));
    if (events == NULL) {
        perror("calloc(3) failed when attempting to allocate events buffer");
        pthread_exit(NULL);
    }

	/* initialize two sockets:
     * - one for incoming client connections
     * - a second for intercommunication between nodes
     */
    int fds[2] = {
        listento(instance.el.host, instance.el.server_port),
        listento(instance.el.host, instance.el.cluster_port)
    };

    int accept_socket;
    struct sockaddr addr;
    socklen_t addrlen = sizeof addr;
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

    /* Number of worker and writer threads to create, should be tweaked based
       on the number of the physical cores provided by the CPU */
    /* worker thread pool */
    pthread_t workers[instance.el.epoll_workers];

    /* I/0 thread pool initialization, allocating a worker_epool structure for
       each one. A worker_epool structure is formed of an epoll descriptor and
       his event queue. Every  worker_epoll is added to a list, in order to
       reuse them in the event loop to add connecting descriptors in a round
       robin scheduling */
    for (int i = 0; i < instance.el.epoll_workers; ++i)
        pthread_create(&workers[i], NULL, worker, NULL);

    int nfds;

    /* Add two already listening nonblocking descriptors to the loop, the first
       one represent the main point of access for clients, the second one is
       responsible for the communication between nodes (bus) */
    for (int n = 0; n < 2; ++n) {
        ADD_FD(instance.el.epollfd, fds[n]);
	}

    /* Start the main event loop, epoll_wait blocks until an event occur */
    while (1) {

        /* Reset the cycle of the round-robin selection of epoll fds */
        if ((nfds = epoll_wait(instance.el.epollfd,
                        events, instance.el.max_events, -1)) == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < nfds; ++i) {
            if ((events[i].events & EPOLLERR) ||
                    (events[i].events & EPOLLHUP)) {
                /* An error has occured on this fd, or the socket is not
                   ready for reading */
                perror ("epoll error");
                close(events[i].data.fd);
                continue;
            }

            /* If fdescriptor is main server or bus server add it to worker
               threads or to the global epoll instance respectively */
            if (events[i].data.fd == fds[0]
                    || events[i].data.fd == fds[1]) {

				if ((accept_socket = accept(events[i].data.fd,
								(struct sockaddr *) &addr, &addrlen)) == -1) {
                    perror("accept");
                    close(events[i].data.fd);
                }

                getnameinfo(&addr, addrlen, hbuf, sizeof(hbuf),
                        sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);

                set_nonblocking(accept_socket);

                /* Client connection check, this case must add the descriptor
                   to the next worker thread in the list */
                if (events[i].data.fd == fds[0]) {
                    ADD_FD(instance.el.bepollfd, accept_socket);
                    SET_FD_IN(instance.el.epollfd, fds[0]);
                    DEBUG("Client connected %s:%s %d\n",
							hbuf, sbuf, accept_socket);
                }

                /* Bus connection check, in this case, the descriptor
                   represents another node connecting, must be added to the
                   global epoll instance */
                if (events[i].data.fd == fds[1]) {
                    ADD_FD(instance.el.epollfd, accept_socket);
                    SET_FD_IN(instance.el.epollfd, fds[1]);
					DEBUG("Node connected %s:%s %d\n",
							hbuf, sbuf, accept_socket);
                }
            } else if (events[i].events & EPOLLIN) {

                /* There's some data from peer nodes to be processed, check if
                   the lock is released (i.e. the cluster has succesfully
                   formed) and handle incoming messages */
                int done = 0;
                if (instance.lock == 0) {
                    DEBUG("Handling request from %d\n", events[i].data.fd);
					done = (instance.el.cluster_handler)(events[i].data.fd);
                    if (done == END) {
                        /* close the connection */
                        DEBUG("Closing connection request\n");
                        close(events[i].data.fd);
                        break;
                    }
				}
            }
            else if (events[i].events & EPOLLOUT) {
                peer_t *p = (peer_t *) events[i].data.ptr;
				DEBUG("Send to fd %d\n", p->fd);
                SET_FD_IN(instance.el.epollfd, p->fd);
                if (send_all(p->fd, p->data, (int *) &p->size) < 0)
                    perror("Send data failed");
                /* Check if struct udata contains allocated memory */
                if (p->alloc == 1) free(p->data);
                free(p);
            }
        }
    }
	free(events);
    return 0;
}


/*
 * Add peer_t structure to the global write queue, a writer worker will
 * handle it
 */
void schedule_write(peer_t *p) {
	if (p->tocli == 0) {
        DEBUG("Scheduled write to peer fd: %d\n", p->fd);
        SET_FD_OUT(instance.el.epollfd, p->fd, p);
    } else {
        DEBUG("Scheduled write to client\n");
        SET_FD_OUT(instance.el.bepollfd, p->fd, p);
    }
}
