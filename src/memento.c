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
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "util.h"
#include "cluster.h"
#include "networking.h"


/*
 * The only thread used in the system, just for connect all nodes defined
 * in the configuration file
 */
static void *form_cluster_thread(void *p) {

    int len = 0;
    sleep(2);

    while (instance.lock == 1) {

        list_node *cursor = instance.cluster->head;

        while (cursor) {
            /* count for UNREACHABLE nodes */
            len = cluster_unreachable_count();
            cluster_node *n = (cluster_node *) cursor->data;
            if (cluster_reachable(n) == 0) {
                DEBUG("Trying to connect to cluster node %s:%d\n",
                        n->addr, n->port);
                char p[5];
                sprintf(p, "%d", n->port);
                if (cluster_join(n->addr, p) == 1) len--;
            }
            cursor = cursor->next;
        }

        if (len <= 0) instance.lock = 0; // all nodes are connected, release lock

        sleep(3);
    }

    DEBUG("Cluster node succesfully joined to the cluster\n");
    cluster_balance();
    //DEBUG("All cluster nodes are balanced\n");
	DEBUG("Keyspace correctly balanced\n");
    // debug check
    // FIXME: remove
    list_node *cursor = instance.cluster->head;
    while (cursor) {
        cluster_node *n = (cluster_node *) cursor->data;
		DEBUG("%s:%d -> [%u,%u]\n", n->addr, n->port, n->range_min, n->range_max);
        //DEBUG("Node: %s:%d - Min: %u Max: %u Name: %s Fd: %d\n",
        //        n->addr, n->port, n->range_min, n->range_max, n->name, n->fd);
        cursor = cursor->next;
    }
    LOG(INFO, "Cluster succesfully formed\n");
    return NULL;
}


int main(int argc, char **argv) {

    /* Initialize random seed */
    srand((unsigned int) time(NULL));
    const char *home = get_homedir();
    char *address = "127.0.0.1";
    char *port = "8082";
    char *confpath = "/.memento";    // default path for configuration ~/.memento
    char *filename = malloc(strlen(home) + strlen(confpath));
    sprintf(filename, "%s%s", home, confpath);
    char *id = NULL;
    int opt, cluster_mode = 0;
    static pthread_t thread;

    while((opt = getopt(argc, argv, "a:i:p:cf:")) != -1) {
        switch(opt) {
            case 'a':
                address = optarg;
                break;
            case 'p':
                port = optarg;
                break;
            case 'i':
                id = optarg;
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

    if (GETINT(port) > 65435) {
        fprintf(stderr, "Port must be at lesser than or equal to 65435\n");
        exit(EXIT_FAILURE);
    }

    char bus_port[20];
    int bport = GETINT(port) + 100;
    sprintf(bus_port, "%d", bport);

    /* If cluster mode is enabled Initialize cluster map */
    if (cluster_mode == 1) {

		INFO("Cluster mode started, reading configuration\n");

		init_system(1, id, address, port, bus_port);

        /* read cluster configuration */
        FILE *file = fopen(filename, "r");
        char line[256];
        int linenr = 0;

        if (file == NULL) {
            perror("Couldn't open the configuration file\n");
            free(filename);
            exit(EXIT_FAILURE);
        }

        while (fgets(line, 256, (FILE *) file) != NULL) {

            char *ip = malloc(15), *pt = malloc(5), *name = calloc(1, 256);
            int self_flag = 0;
            linenr++;

            /* skip comments line */
            if (line[0] == '#') continue;

            if (sscanf(line, "%s %s %s %d", ip, pt, name, &self_flag) != 4) {
                fprintf(stderr, "Syntax error, line %d\n", linenr);
                continue;
            }

            DEBUG("[CFG] Line %d: IP %s PORT %s NAME %s SELF %d\n",
                    linenr, ip, pt, name, self_flag);

            /* create a new node and add it to the list */
            cluster_node *new_node = shb_malloc(sizeof(cluster_node));
            new_node->addr = ip;
            new_node->port = GETINT(pt);
            new_node->state = UNREACHABLE;

            if (self_flag == 1) {
                /* check if the node is already present */
                if (cluster_contained(new_node) == 1) {
                    free(new_node);
                    if (!id) cluster_set_selfname(name);
                    continue;
                } else {
                    new_node->self = 1;
                    new_node->state = REACHABLE;
                }
                if (!id) cluster_set_selfname(name);
            }
            else new_node->self = 0;

            new_node->name = name;

            /* add the node to the cluster list */
            cluster_add_node(new_node);
        }

        fclose(file);

        /* start forming the thread */
        if (pthread_create(&thread, NULL, &form_cluster_thread, NULL) != 0)
            perror("ERROR pthread");

    } else {

        /* single node mode */
		init_system(0, id, address, port, bus_port);
    }

    /* start the main listen loop */
	start_loop();
    return 0;
}
