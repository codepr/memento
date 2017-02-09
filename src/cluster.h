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

#ifndef CLUSTER_H
#define CLUSTER_H

#include "map.h"
#include "list.h"
#include "util.h"
#include "networking.h"
#include <sys/epoll.h>

#define CMD_BUFSIZE (1024)
#define PARTITIONS (8192)   // whole keyspace


typedef enum { REACHABLE, UNREACHABLE } state;

/* Cluster node structure */
typedef struct {
    const char *name;       // node name, a 64 byte len string
    const char *addr;       // node ip address
    int port;               // node port
    int fd;                 // node file descriptor
    state state;            // current node state in the cluster
    unsigned int self : 1;  // define if the node is a seed or not
    unsigned int range_min; // key range lower bound
    unsigned int range_max; // key range upper bound
} cluster_node;

/* Global shared state of the system, it represents the distributed map */
typedef struct {
    unsigned int lock : 1;              // global lock, used in a cluster context
    unsigned int cluster_mode : 1;      // distributed flag
    int epollfd;                        // file descriptor for epoll
    struct epoll_event ev;              // global epoll
    struct epoll_event evs[MAX_EVENTS]; // global event loop
    map *store;                         // items of the DB
    list *cluster;                      // map of cluster nodes
    list *ingoing;                      // map of the ingoing connection as backup
    loglevel log_level;                 // log level of the entire system
} shibui;


extern shibui instance;


void cluster_start(int, int *, size_t, map *, map *);
int cluster_init(int, const char *, const char *, const char *);
void cluster_add_node(cluster_node *);
cluster_node *cluster_get_node(const char *, const char *);
int cluster_contained(cluster_node *);
int cluster_fd_contained(int);
int cluster_reachable(cluster_node *);
int cluster_unreachable_count(void);
int cluster_set_state(cluster_node *, state);
int cluster_join(const char *, const char *);
void cluster_balance(void);

#endif
