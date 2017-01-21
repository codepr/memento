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
#include <pthread.h>
#include "util.h"
#include "commands.h"
#include "queue.h"
#include "cluster.h"
#include "hashing.h"
#include "serializer.h"
#include "networking.h"
#include "messagequeue.h"


static void *consume_queue(void *param) {
    struct consume_params *params = (struct consume_params *) param;
    int *slaves = params->slaves;
    int *len = params->len;
    queue *q = params->q;
    uint32_t seed = RANDBETWEEN(0, 32768); // initial seed for murmur hashing
    while(1) {
        if (q->last > 0) {
            char *deq_mex = (char *) dequeue(q);
            char *metadata = deq_mex;
            int cmd_len = *((int*) metadata) + (sizeof(int) * 2);
            struct message mm = deserialize(deq_mex);
            LOG("Deserialized..\r\n");
            char *des_mex = mm.content;
            char *command = NULL;
            command = strtok(des_mex, " \r\n");
            LOG("Command : %s\n", command);
            char *arg_1 = NULL;
            arg_1 = strtok(NULL, " ");
            LOG("Arg1 : %s\n", arg_1);
            if (arg_1) {
                char *arg_1_holder = malloc(strlen(arg_1));
                strcpy(arg_1_holder, arg_1);
                trim(arg_1_holder);
                int idx = murmur3_32((const uint8_t *) arg_1_holder, strlen(arg_1_holder), seed) % (*len);
                LOG("Hash of key %s with len %d is %d\n", arg_1_holder, (*len), idx);
                send(slaves[idx], deq_mex, cmd_len, 0);
            }
        } else continue;
    }
    return NULL;
}


static void *redirect_to_queue(void *param) {
    struct redirect_params *params = (struct redirect_params *) param;
    int fd = params->fd;
    queue *q = params->q;
    while(1) {
        if (q->last > 0) {
            LOG("Redirecting..\r\n");
            char *deq_mex = (char *) dequeue(q);
            char *metadata = deq_mex;
            int cmd_len = *((int*) metadata) + (sizeof(int) * 2);
            send(fd, deq_mex, cmd_len, 0);
        }
    }
    return NULL;
}


void mq_seed_gateway(queue *mqueue, int distributed, map_t map) {

    char *hostname = "127.0.0.1";
    int p = atoi(MQ_PORT);
    // start consuming loop thread
    static pthread_t t;
    struct sockaddr_in serveraddr;
    struct hostent *server;
    /* socket: create the socket */
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0)
        perror("ERROR opening socket");

    /* gethostbyname: get the server's DNS entry */
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr, "ERROR, no such host as %s\n", hostname);
        exit(0);
    }

    /* build the server's address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *) server->h_addr,
            (char *) &serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(p);

    /* connect: create a connection with the server */
    if (connect(sock_fd, (const struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0) {
        // First messagequeue started
        // init slaves' fd array
        int *slaves = (int *) malloc(sizeof(int) * MAX_SLAVES);
        int last = 0;
        struct consume_params *params =
            (struct consume_params *)  malloc(sizeof(struct consume_params));
        params->q = mqueue;
        params->slaves = slaves;
        params->len = &last;
        // obtain an handle
        struct s_handle *handle = create_async_server("127.0.0.1", MQ_PORT);
        int efd = handle->efd;
        int sfd = handle->sfd;
        int s = handle->s;
        struct epoll_event event = handle->event;
        struct epoll_event *events = handle->events;
        if (pthread_create(&t, NULL, &consume_queue, params) != 0)
            perror("ERROR pthread");

        while (1) {
            int n, i;

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
                            /* char time_buff[100]; */
                            /* time_t now = time(0); */
                            /* strftime(time_buff, 100, "<*> [%Y-%m-%d %H:%M:%S]", localtime(&now)); */
                            LOG("New connection from %s:%s on descriptor %d \n", hbuf, sbuf, infd);
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
                        /* slaves[last++] = infd; */
                        /* char set_message[4]; */
                        /* sprintf(set_message, "#%d%d", last - 1, last); */
                        /* struct message set_m; */
                        /* set_m.content = set_message; */
                        /* set_m.fd = infd; */
                        /* send(infd, serialize(set_m), 12, 0); // 12 = content size + sizeof(int) * 2 */
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

                        struct message mm = deserialize(buf);
                        char *m = mm.content;
                        int f = mm.fd;
                        if (m[0] == '*') {
                            LOG("Response ..\r\n");
                            send(f, m + 2, strlen(m) - 2, 0);
                        } else if (buf[0] == '^') {
                            // join request
                            LOG("Join request\r\n");
                            slaves[last++] = events[i].data.fd;
                        } else {
                            // it should be already serialized
                            LOG("Receiving ..\r\n");
                            enqueue(mqueue, serialize(mm));
                            /* enqueue(mqueue, buf); */
                        }
                        free(buf);
                    }

                    if (done) {
                        last--;
                        LOG("Closed connection on descriptor %d\n",
                                events[i].data.fd);
                        /* Closing the descriptor will make epoll remove it from the
                           set of descriptors which are monitored. */
                        close(events[i].data.fd);
                    }
                }
            }
        }
    } else {

        // Connect to an existing message queue and redirect all serialized message
        // to it

        struct redirect_params *params =
            (struct redirect_params *) malloc(sizeof(struct redirect_params));
        params->fd = sock_fd;
        params->q = mqueue;

        if (pthread_create(&t, NULL, &redirect_to_queue, params) != 0)
            perror("ERROR pthread");

        while(1) {}
        /* char *buf = (char *) malloc(CMD_BUFSIZE); */
        /* bzero(buf, CMD_BUFSIZE); */
        /* int done = 0; */
        /* int id, slave_number = 0; */
        /*  */
        /* while(1) { */
        /*     while(read(sock_fd, buf, CMD_BUFSIZE) > 0) { */
        /*         #<{(| enqueue(mqueue, buf); |)}># */
        /*         struct message mm = deserialize(buf); */
        /*         char *m = mm.content; */
        /*         if (m[0] == '#') { */
        /*             // first connection, set number of slaves and number assigned */
        /*             id = GETINT(m + 1); // first position handle id */
        /*             slave_number = GETINT(m + 2); // second position handle slave number */
        /*             printf("<*> Refreshed Node ID: %d and total slaves: %d\n", id, slave_number); */
        /*         } else { */
        /*             printf("<*> Request from master -> %s - %d\n", mm.content, mm.fd); */
        /*             int proc = process_command(distributed, map, m, sock_fd, mm.fd); */
        /*             struct message to_be_sent; */
        /*             to_be_sent.fd = mm.fd; */
        /*             size_t meta_len = sizeof(int) * 2; */
        /*             size_t data_len = meta_len; */
        /*  */
        /*             switch(proc) { */
        /*                 case MAP_OK: */
        /*                     to_be_sent.content = S_D_OK; */
        /*                     data_len += sizeof(S_D_OK); */
        /*                     break; */
        /*                 case MAP_MISSING: */
        /*                     to_be_sent.content = S_D_NIL; */
        /*                     data_len += sizeof(S_D_NIL); */
        /*                     break; */
        /*                 case MAP_FULL: */
        /*                 case MAP_OMEM: */
        /*                     to_be_sent.content = S_D_OOM; */
        /*                     data_len += sizeof(S_D_OOM); */
        /*                     break; */
        /*                 case COMMAND_NOT_FOUND: */
        /*                     to_be_sent.content = S_D_UNK; */
        /*                     data_len += sizeof(S_D_UNK); */
        /*                     break; */
        /*                 case END: */
        /*                     done = 1; */
        /*                     break; */
        /*                 default: */
        /*                     continue; */
        /*                     break; */
        /*             } */
        /*  */
        /*             // send out data */
        /*             send(sock_fd, serialize(to_be_sent), data_len, 0); */
        /*  */
        /*             bzero(buf, CMD_BUFSIZE); */
        /*             if (done) { */
        /*                 printf("Closed connection on descriptor %d\n", sock_fd); */
        /*                 #<{(| Closing the descriptor will make epoll remove it from the */
        /*                    set of descriptors which are monitored. |)}># */
        /*                 close(sock_fd); */
        /*             } */
        /*         } */
        /*     } */
        /*     free(buf); */
        /* } */
    }
}

