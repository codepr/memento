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
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "util.h"
#include "cluster.h"
#include "commands.h"
#include "networking.h"


static void *form_cluster_thread(void *p) {

    sleep(2);

    while (instance.lock) {

        list_node *cursor = instance.cluster->head;

        while(cursor) {
            cluster_node *n = (cluster_node *) cursor->data;
            LOG("Trying to connect to cluster node %s:%d", n->addr, n->port);
            if (cluster_reachable(n) == 0) {
                char p[5];
                sprintf(p, "%d", n->port);
                cluster_join(n->addr, p);
            }
            cursor = cursor->next;
        }

        sleep(3);
    }
    return NULL;
}


int main(int argc, char **argv) {

    /* Initialize random seed */
    srand((unsigned int) time(NULL));
    char *address = "127.0.0.1";
    char *port = "6373";
    char *filename = "./config";
    int opt, cluster_mode = 0;
    static pthread_t thread;

    while((opt = getopt(argc, argv, "a:p:cf:")) != -1) {
        switch(opt) {
            case 'a':
                address = optarg;
                break;
            case 'p':
                port = optarg;
                break;
            case 'c':
                cluster_mode = 1;
                break;
            case 'f':
                filename = optarg;
                break;
            default:
                cluster_mode = 0;
                break;
        }
    }

    char bus_port[20];
    int bport = GETINT(port) + 100;
    sprintf(bus_port, "%d", bport);

    /* If cluster mode is enabled Initialize cluster map */
    if (cluster_mode == 1) {

        cluster_init(1);

        /* read cluster configuration */
        FILE *file = fopen(filename, "r");
        char line[256];
        int linenr = 0;

        while (fgets(line, 256, (FILE *) file) != NULL) {

            char ip[256], pt[256];
            linenr++;

            /* skip comments line */
            if (line[0] == '#') continue;

            if (sscanf(line, "%s %s", ip, pt) != 2) {
                fprintf(stderr, "Syntax error, line %d\n", linenr);
                continue;
            }

            LOG("[CFG] Line %d: IP %s PORT %s\n", linenr, ip, pt);

            /* create a new node and add it to the list */
            cluster_node *new_node =
                (cluster_node *) shb_malloc(sizeof(cluster_node));
            new_node->addr = ip;
            new_node->port = GETINT(pt);
            new_node->state = UNREACHABLE;
            new_node->seed = 0;

            /* add the node to the cluster list */
            cluster_add_node(new_node);

        }

        /* start forming the thread */
        if (pthread_create(&thread, NULL, &form_cluster_thread, NULL) != 0)
            perror("ERROR pthread");

        /* initialize two sockets:
         * - one for incoming client connections
         * - a second for intercommunication between nodes
         */
        int sockets[2] = {
            listento(address, port),
            listento(address, bus_port)
        };


        /* MUST HANDLE CLUSTER WIDE */

        /* handler function for incoming data */
        fd_handler handler_ptr = &command_handler;
        event_loop(sockets, 2, handler_ptr);

    } else {

        cluster_init(0);
        /* initialize two sockets:
         * - one for incoming client connections
         * - a second for intercommunication between nodes
         */
        int sockets[2] = {
            listento(address, port),
            listento(address, bus_port)
        };

        /* handler function for incoming data */
        fd_handler handler_ptr = &command_handler;

        event_loop(sockets, 2, handler_ptr);

    }

    return 0;
}
