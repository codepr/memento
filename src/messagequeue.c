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
#include "queue.h"
#include "hashing.h"
#include "serializer.h"
#include "networking.h"
#include "messagequeue.h"



static void *consume_queue(void *param) {
    struct consume_params *params = (struct consume_params *) param;
    int *slaves = params->slaves;
    int *len = params->len;
    queue *q = params->q;
    uint32_t seed = RANDBETWEEN(0, 32768);
    while(1) {
        if (q->last > 0) {
            char *deq_mex = (char *) dequeue(q);
            char *metadata = deq_mex;
            int sLen = *((int*)metadata) + (sizeof(int) * 2);
            struct message mm = deserialize(deq_mex);
            char *des_mex = mm.content;
            char *command = NULL;
            command = strtok(des_mex, " \n");
            printf("Command : %s\n", command);
            char *arg_1 = NULL;
            /* char *arg_2 = NULL; */
            arg_1 = strtok(NULL, " ");
            printf("Arg1 : %s\n", arg_1);
            if (arg_1) {
                char *arg_1_holder = malloc(strlen(arg_1));
                strcpy(arg_1_holder, arg_1);
                trim(arg_1_holder);
                int idx = murmur3_32((const uint8_t *)arg_1_holder, strlen(arg_1_holder), seed) % (*len);
                printf("Hash of key %s with len %d is %d\n", arg_1_holder, (*len), idx);
                send(slaves[idx], deq_mex, sLen, 0);
            }
        } else continue;
    }
    return NULL;
}


void mq_seed_gateway(queue *mqueue) {
    // init slaves' fd array
    int *slaves = (int *) malloc(sizeof(int) * MAX_SLAVES);
    int last = 0;
    struct consume_params *params =
        (struct consume_params *)  malloc(sizeof(struct consume_params));
    params->q = mqueue;
    params->slaves = slaves;
    params->len = &last;
    // start consuming loop thread
    static pthread_t t;
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
                    slaves[last++] = infd;
                    char set_message[4];
                    sprintf(set_message, "#%d%d", last - 1, last);
                    struct message set_m;
                    set_m.content = set_message;
                    set_m.fd = infd;
                    send(infd, serialize(set_m), 12, 0); // 12 = content size + sizeof(int) * 2
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
                        send(f, m, strlen(m), 0);
                    }
                    else {
                        // it should be already serialized
                        printf("Receiving ");
                        enqueue(mqueue, buf);
                    }
                    free(buf);
                }

                if (done) {
                    last--;
                    printf("Closed connection on descriptor %d\n",
                            events[i].data.fd);
                    /* Closing the descriptor will make epoll remove it from the
                       set of descriptors which are monitored. */
                    close(events[i].data.fd);
                }
            }
        }
    }
}
