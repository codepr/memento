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

#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "list.h"
#include "event.h"
#include "cluster.h"

/* Global state store instance */
memento instance;

/* Self reference in a cluster context */
cluster_node self;


/*
 * Initialize the global shared structure, representing the cluster and the
 * store itself
 */
void init_system(int distributed, const char *id,
		const char *host, char *server_port, char *cluster_port) {

    /* Initialize instance containers */
    instance.cluster_mode = distributed;
    instance.store = map_create();
    instance.cluster = list_create();
    instance.log_level = DEBUG;

	init_event_loop(host, server_port, cluster_port);

    /* check for distribution */
    if (distributed == 1) {
        /* Initialize self reference */
        self.state = REACHABLE;
        self.self = 1;
        self.addr = host;
        self.port = GETINT(cluster_port);
        self.name = id;
        /* lock for incoming connection, till the cluster is formed and ready */
        instance.lock = 1;
    }
	else instance.lock = 0;
}


/*
 * Deallocate all structures in a cluster context
 */
void cluster_destroy(void) {
    /* deallocate instance containers */
    map_release(instance.store);
    list_release(instance.cluster);
}


/*
 * Add a cluster node to the global state memento
 */
void cluster_add_node(cluster_node *node) {
    instance.cluster = list_head_insert(instance.cluster, node);
}


/*
 * Check if the cluster node is already present in the list, just a linear
 * search on the cluster nodes list
 */
int cluster_contained(cluster_node *node) {
    /* Start from head node */
    list_node *cursor = instance.cluster->head;
    /* cycle till cursor != NULL */
    while (cursor) {
        cluster_node *n = (cluster_node *) cursor->data;
        if ((strncmp(n->addr, node->addr, strlen(node->addr))) == 0
                && n->port == node->port
                && (strncmp(n->name, node->name, strlen(node->name)) == 0)) {
            /* found a match */
            return 1;
        }
        cursor = cursor->next; // move the pointer forward
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
 * Count the number of cluster nodes in a state of UNREACHABLE
 */
int cluster_unreachable_count(void) {
    list_node *cursor = instance.cluster->head;
    int count = 0;

    while (cursor) {
        if (((cluster_node *) cursor->data)->state == UNREACHABLE) count++;
        cursor = cursor->next;
    }
    return count;
}


/*
 * Sets the cluster node contained in the cluster list to state st
 * instance.
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
cluster_node *cluster_get_node(const char *host, char *port) {
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
 * Add a node to the cluster by adding it to the cluster list of nodes
 */
int cluster_join(const char *host, char *port) {
    int fd;
    if ((fd = connectto(host, port)) == -1) {
        //ERROR("Impossible connection to %s:%s\n", host, port);
		ERROR("Awaiting for peer %s:%s to accept connection\n", host, port);
        return -1;
    }
    /* If cluster node is present set the file descriptor */
    cluster_node *n = cluster_get_node(host, port);
    if (n) {
        n->fd = fd;
        if (cluster_reachable(n) == 0) cluster_set_state(n, REACHABLE);
    } else {
        /* create a new node and add it to the list */
        cluster_node *new_node = shb_malloc(sizeof(cluster_node));
        new_node->addr = host;
        new_node->port = GETINT(port);
        new_node->fd = fd;
        new_node->state = REACHABLE;
        new_node->self = 0;
        instance.cluster =
            list_head_insert(instance.cluster, new_node);
    }
    ADD_FD(instance.el.epollfd, fd);
    return 1;
}


/*
 * Balance the cluster by giving a correct key range to every node, resulting
 * in a sorted list of balanced nodes
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


/*
 * Sets the name of the self node of the cluster, just for utility usages
 */
void cluster_set_selfname(const char *name) {
    self.name = strdup(name);
}
