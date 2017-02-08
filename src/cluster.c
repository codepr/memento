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

/* #include <sys/socket.h> */
/* #include <netinet/in.h> */
/* #include <unistd.h> */
/* #include <stdio.h> */
/* #include <stdlib.h> */
/* #include <netdb.h> */
/* #include <fcntl.h> */
/* #include <time.h> */
#include <string.h>
#include "util.h"
#include "list.h"
#include "cluster.h"
/* #include "commands.h" */


shibui instance;


/*
 * Initialize the global shared structure, representing the cluster and the
 * store itself
 */
int cluster_init(int distributed, const char *id, const char *host, const char *port) {
    instance.cluster_mode = distributed;
    instance.store = map_create();
    instance.cluster = list_create();
    instance.ingoing = list_create();
    /* initialized global epollfd */

    instance.ev.events = EPOLLIN | EPOLLET;
    if ((instance.epollfd = epoll_create1(0)) == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    /* check for distribution */
    if (distributed == 1) {
        /* insert self node */
        cluster_node *self =
            (cluster_node *) malloc(sizeof(cluster_node));
        self->name = id;
        self->self = 1;
        self->addr = host;
        self->port = GETINT(port);
        self->state = REACHABLE;
        instance.cluster =
            list_head_insert(instance.cluster, self);
        instance.lock = 1;
    }
    else instance.lock = 0;
    if (instance.store != NULL)
        return 0;
    else return -1;
}


/*
 * Add a cluster node to the global state shibui
 */
void cluster_add_node(cluster_node *node) {
    instance.cluster = list_head_insert(instance.cluster, node);
}


/*
 * Check if the cluster node is already present in the list
 */
int cluster_contained(cluster_node *node) {
    /* Start from head node */
    list_node *cursor = instance.cluster->head;
    /* cycle till cursor != NULL */
    while (cursor) {
        cluster_node *n = (cluster_node *) cursor->data;
        if ((strncmp(n->addr, node->addr, strlen(node->addr))) == 0
                && n->port == node->port) {
            /* found a match */
            return 1;
        }
        cursor = cursor->next; // move the pointer forward
    }
    /* node is not present */
    return 0;
}


/*
 * Check if the cluster node is already present in the list
 * FIXME: repeated code
 */
int cluster_fd_contained(int fd) {
    /* Start from head node */
    list_node *cursor = instance.cluster->head;
    /* cycle till cursor != NULL */
    while (cursor) {
        cluster_node *n = (cluster_node *) cursor->data;
        if (n->fd == fd) {
            /* found a match */
            return 1;
        }
        cursor = cursor->next; // move the pointer forward
    }

    cursor = instance.ingoing->head;
    while(cursor) {
        cluster_node *n = (cluster_node *) cursor->data;
        if (n->fd == fd) return 1;
        cursor = cursor->next;
    }
    /* node is not present */
    return 0;
}


/*
 * Checks if the cluster node is in a REACHABLE state
 */
int cluster_reachable(cluster_node *node) {
    if (node->state == REACHABLE) return 1;
    else return 0;
}


/*
 * Sets the cluster node contained in the cluster list to state st
 */
int cluster_set_state(cluster_node *node, state st) {
    /* Start from head node */
    list_node *cursor = instance.cluster->head;
    /* cycle till cursor != NULL */
    while (cursor) {
        cluster_node *n = (cluster_node *) cursor->data;
        if ((strncmp(n->addr, node->addr, strlen(node->addr))) == 0
                && n->port == node->port) {
            /* found a match */
            n->state = st;
            return 1;
        }
        cursor = cursor->next; // move the pointer forward
    }
    /* node is not present */
    return 0;
}


/*
 * Return the associated cluster node to the host and port specified
 * FIXME: repeated code
 */
cluster_node *cluster_get_node(const char *host, const char *port) {
    /* Start from head node */
    list_node *cursor = instance.cluster->head;
    /* cycle till cursor != NULL */
    while (cursor) {
        cluster_node *n = (cluster_node *) cursor->data;
        if ((strncmp(n->addr, host, strlen(host))) == 0
                && n->port == GETINT(port)) {
            /* found a match */
            return n;
        }
        cursor = cursor->next; // move the pointer forward
    }
    /* node is not present */
    return NULL;

}

/*
 * Add a node to the cluster by joining
 */
int cluster_join(const char *host, const char *port) {
    int fd;
    if ((fd = connectto(host, port)) == -1) {
        fprintf(stderr, "[!] Impossible connection to %s:%s\n", host, port);
        return -1;
    }
    /* If cluster node is present set the file descriptor */
    cluster_node *n = cluster_get_node(host, port);
    if (n) {
        n->fd = fd;
        if (cluster_reachable(n) == 0) cluster_set_state(n, REACHABLE);
    } else {
        /* create a new node and add it to the list */
        cluster_node *new_node =
            (cluster_node *) shb_malloc(sizeof(cluster_node));
        new_node->addr = host;
        new_node->port = GETINT(port);
        new_node->fd = fd;
        new_node->state = REACHABLE;
        new_node->self = 0;
        instance.cluster =
            list_head_insert(instance.cluster, new_node);
    }
    instance.ev.data.fd = n->fd;

    if(epoll_ctl(instance.epollfd, EPOLL_CTL_ADD, n->fd, &instance.ev) == -1) {
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }

    return 1;
}


/*
 * Balance the cluster by giving a correct key range to every node
 */
void cluster_balance(void) {

    /* Define a step to obtain the ranges */
    int range = 0;
    unsigned long len = instance.cluster->len;
    int step = PARTITIONS / len;

    /* sort the list */
    instance.cluster->head = merge_sort(instance.cluster->head);
    list_node *cursor = instance.cluster->head;

    /* Cycle through the cluster nodes */
    while (cursor) {
        cluster_node *n = (cluster_node *) cursor->data;
        n->range_min = range;
        range += step;
        n->range_max = range - 1;
        if (cursor->next == NULL) n->range_max = PARTITIONS;
        cursor = cursor->next;
    }
}


/* #include "commands.h" */
/* #include "serializer.h" */
/* #include "util.h" */
/*  */
/*  */
/* #<{(| */
/*  * Join the cluster, used by slave nodes to set a connection with the master */
/*  * node */
/*  |)}># */
/* void cluster_join(int distributed, map_t map, const char *hostname, const char *port) { */
/*     int p = atoi(port); */
/*     struct sockaddr_in serveraddr; */
/*     struct hostent *server; */
/*     #<{(| socket: create the socket |)}># */
/*     int sock_fd = socket(AF_INET, SOCK_STREAM, 0); */
/*     if (sock_fd < 0) */
/*         perror("ERROR opening socket"); */
/*  */
/*     #<{(| gethostbyname: get the server's DNS entry |)}># */
/*     server = gethostbyname(hostname); */
/*     if (server == NULL) { */
/*         fprintf(stderr, "ERROR, no such host as %s\n", hostname); */
/*         exit(0); */
/*     } */
/*  */
/*     #<{(| build the server's address |)}># */
/*     bzero((char *) &serveraddr, sizeof(serveraddr)); */
/*     serveraddr.sin_family = AF_INET; */
/*     bcopy((char *) server->h_addr, */
/*             (char *) &serveraddr.sin_addr.s_addr, server->h_length); */
/*     serveraddr.sin_port = htons(p); */
/*  */
/*     #<{(| connect: create a connection with the server |)}># */
/*     if (connect(sock_fd, (const struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0) { */
/*         perror("ERROR connecting"); */
/*         exit(0); */
/*     } */
/*  */
/*     // join request */
/*     send(sock_fd, "^", 1, 0); */
/*  */
/*     char *buf = (char *) malloc(CMD_BUFSIZE); */
/*     bzero(buf, CMD_BUFSIZE); */
/*     int done = 0; */
/*  */
/*     while(1) { */
/*         while(read(sock_fd, buf, CMD_BUFSIZE) > 0) { */
/*             struct message mm = deserialize(buf); */
/*             char *m = mm.content; */
/*             LOG("Request: %s", mm.content, mm.fd); */
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
/*                 LOG("Closed connection on descriptor %d\n", sock_fd); */
/*                 #<{(| Closing the descriptor will make epoll remove it from the */
/*                    set of descriptors which are monitored. |)}># */
/*                 close(sock_fd); */
/*             } */
/*         } */
/*         free(buf); */
/*     } */
/* } */
/*  */

