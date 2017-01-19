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
#include <time.h>
#include <pthread.h>
#include "util.h"
#include "server.h"
#include "commands.h"
#include "cluster.h"
#include "persistence.h"
#include "networking.h"
#include "serializer.h"

#define COMMAND_NOT_FOUND (-4)
#define EXPIRATION_CHECK_INTERVAL (3) // check every 3s

static unsigned int is_checking = 0; /* expire-time-key-check thread flag */

/* Check every key expire time, if any is set, remove the keys that has exceeded
 * their lifetime
 */
static int check_expire_time(any_t t1, any_t t2) {
    h_map *m = (h_map *) t1;
    kv_pair *kv = (kv_pair *) t2;
    if (m && kv) {
        if (kv->in_use == 1 && kv->has_expire_time != 0) {
            long current_ms = current_timestamp();
            long delta = current_ms - kv->creation_time;
            if (delta >= kv->expire_time) {
                trim(kv->key);
                m_remove(m, kv->key);
            }
        }
    }
    return 1;
}

/* daemon to control all expire time of keys in the partitions, iterates through
 * the entire keyspace checking the expire time of every key that has one set
 */
static void *expire_control_pthread(void *arg) {
    /* partition **buckets = (partition **) arg; */
    h_map *m = (h_map *) arg;
    if (m) {
        while(1) {
            if (m->size > 0) {
                m_iterate(m, check_expire_time, m);
            }
            sleep(EXPIRATION_CHECK_INTERVAL);
        }
    }
    return NULL;
}

// start server instance, by setting hostname
void start_server(queue *mqueue, int distributed, int master, map_t map, const char *host, const char* port) {
    // partition buckets, every bucket can contain a variable number of
    // key-value pair
    static pthread_t t;
    struct stat st;
    if (stat(PERSISTENCE_LOG, &st) == -1)
        mkdir(PERSISTENCE_LOG, 0644);
    // start expiration time checking thread
    if (is_checking == 0) {
        if (pthread_create(&t, NULL, &expire_control_pthread, map) != 0)
            perror("ERROR pthread");
        is_checking = 1;
    }
    struct s_handle *handle = create_async_server(host, port);
    int efd = handle->efd;
    int sfd = handle->sfd;
    int s = handle->s;
    struct epoll_event event = handle->event;
    struct epoll_event *events = handle->events;

    /* start the event loop */
    while (1) {
        int n, i;

        /* n = epoll_wait(efd, events, MAX_EVENTS, -1); */
        n = epoll_wait(efd, events, MAX_EVENTS, -1);
        for (i = 0; i < n; i++) {
            if ((events[i].events & EPOLLERR) ||
                    (events[i].events & EPOLLHUP) ||
                    (!(events[i].events & EPOLLIN))) {
                perror("epoll error");
                close(events[i].data.fd);
                continue;
            }

            else if (sfd == events[i].data.fd) {
                /* We have a notification on the listening socket, which means
                   one or more incoming connections. */
                while (1) {
                    struct sockaddr in_addr;
                    socklen_t in_len;
                    int infd;
                    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                    in_len = sizeof in_addr;
                    infd = accept(sfd, &in_addr, &in_len);
                    if (infd == -1) {
                        if ((errno == EAGAIN) ||
                                (errno == EWOULDBLOCK)) {
                            /* processed all incoming connections. */
                            break;
                        }
                        else {
                            perror("accept");
                            break;
                        }
                    }

                    s = getnameinfo(&in_addr, in_len,
                            hbuf, sizeof hbuf,
                            sbuf, sizeof sbuf,
                            NI_NUMERICHOST | NI_NUMERICSERV);
                    if (s == 0) {
                        char time_buff[100];
                        time_t now = time(0);
                        strftime(time_buff, 100, "<*> [%Y-%m-%d %H:%M:%S]", localtime(&now));
                        printf("%s - New connection from %s:%s on descriptor %d \n", time_buff,  hbuf, sbuf, infd);
                    }

                    /* Make the incoming socket non-blocking and add it to the
                       list of fds to monitor. */
                    s = set_socket_non_blocking(infd);
                    if (s == -1)
                        abort();

                    event.data.fd = infd;
                    event.events = EPOLLIN | EPOLLET;
                    s = epoll_ctl(efd, EPOLL_CTL_ADD, infd, &event);
                    if (s == -1) {
                        perror("epoll_ctl");
                        abort();
                    }
                }
                continue;
            }
            else {
                /* We have data on the fd waiting to be read */
                int done = 0;

                while (1) {
                    ssize_t count;
                    char *buf = malloc(MAX_DATA_SIZE);
                    bzero(buf, MAX_DATA_SIZE);
                    count = read(events[i].data.fd, buf, MAX_DATA_SIZE - 1);
                    if (count == -1) {
                        /* If errno == EAGAIN, that means we have read all data.
                           So go back to the main loop. */
                        if (errno != EAGAIN) {
                            perror("read");
                            done = 1;
                        }
                        free(buf);
                        break;
                    }
                    else if (count == 0) {
                        /* End of file. The remote has closed the connection. */
                        done = 1;
                        free(buf);
                        break;
                    }
                    struct message msg;
                    msg.content = buf;
                    msg.fd = events[i].data.fd;
                    char *s_mex = serialize(msg);
                    if (distributed == 1 && master == 1) enqueue(mqueue, s_mex);
                    else {
                        int proc = process_command(distributed, map, buf, events[i].data.fd, events[i].data.fd);
                        switch(proc) {
                            case MAP_OK:
                                send(events[i].data.fd, "OK\n", 3, 0);
                                break;
                            case MAP_MISSING:
                                send(events[i].data.fd, "NOT FOUND\n", 10, 0);
                                break;
                            case MAP_FULL:
                            case MAP_OMEM:
                                send(events[i].data.fd, "OUT OF MEMORY\n", 14, 0);
                                break;
                            case COMMAND_NOT_FOUND:
                                send(events[i].data.fd, "COMMAND NOT FOUND\n", 18, 0);
                                break;
                            case END:
                                done = 1;
                                break;
                            default:
                                continue;
                                break;
                        }
                        free(buf);
                    }
                }

                if (done) {
                    printf("Closed connection on descriptor %d\n",
                            events[i].data.fd);
                    /* Closing the descriptor will make epoll remove it from the
                       set of descriptors which are monitored. */
                    close(events[i].data.fd);
                }
            }
        }
    }

    if (master && map)
        m_release(map);
    /* release all partitions */
    /* for (int i = 0; i < PARTITION_NUMBER; i++) */
    /*     partition_release(buckets[i]); */

    free(events);
    close(sfd);

    return;
}
