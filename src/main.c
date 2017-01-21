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
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include "server.h"
#include "cluster.h"
#include "queue.h"
#include "messagequeue.h"

volatile sig_atomic_t running = 1;

struct connection {
    const char *address;
    const char *port;
    int distributed;
    queue *mqueue;
    map_t map;
};


void stop(int signum) {
    running = 0;
    exit(0);
}


static void *cluster_pthread(void *param) {
    struct connection *conn = (struct connection *) param;
    queue *mqueue = conn->mqueue;
    mq_seed_gateway(mqueue);
    return NULL;
}

static void *cluster_join_pthread(void *param) {
    struct connection *conn = (struct connection *) param;
    const char *address = conn->address;
    const char *port = conn->port;
    map_t map = conn->map;
    int distributed = conn->distributed;
    cluster_join(distributed, map, address, port);
    return NULL;
}


int main(int argc, char **argv) {
    srand((unsigned int) time(NULL));
    signal(SIGINT, stop);
    char *address = "127.0.0.1";
    char *port = PORT;
    static pthread_t t;
    int opt;
    int master = 0;
    int distributed = 0;
    queue *mqueue = create_queue();
    map_t map = m_create();

    while((opt = getopt(argc, argv, "a:p:ms")) != -1) {
        switch(opt) {
            case 'a':
                address = optarg;
                break;
            case 'p':
                port = optarg;
                break;
            case 'm':
                master = 1;
                distributed = 1;
                break;
            case 's':
                master = 0;
                distributed = 1;
                break;
            default:
                distributed = 0;
                master = 0;
                break;
        }
    }

    struct connection *conn = (struct connection *) malloc(sizeof(struct connection));
    conn->address = address;
    conn->port = MQ_PORT;
    conn->mqueue = mqueue;
    conn->map = map;
    conn->distributed = distributed;

    if (distributed == 1) {
        if (master == 1) {
            if (pthread_create(&t, NULL, &cluster_pthread, conn) != 0)
                perror("ERROR pthread");
            start_server(mqueue, 1, distributed, map, address, port);
        } else {
            if (pthread_create(&t, NULL, &cluster_join_pthread, conn) != 0)
                perror("ERROR pthread");
            while(1) {}
        }
    } else start_server(mqueue, 0, 0, map, address, port);

    return 0;
}
