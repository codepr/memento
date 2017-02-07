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
#include <time.h>
#include "cluster.h"
#include "commands.h"
#include "networking.h"



int main(int argc, char **argv) {
    /* Initialize random seed */
    srand((unsigned int) time(NULL));
    char *address = "127.0.0.1";
    char *port = "6373";
    char *seed_addr = NULL;
    char *seed_port = NULL;
    int opt, seed = 0, cluster_mode = 0;

    while((opt = getopt(argc, argv, "a:p:cSA:P:")) != -1) {
        switch(opt) {
            case 'a':
                address = optarg;
                break;
            case 'p':
                port = optarg;
                break;
            case 'c':
                cluster_mode = 1;
            case 'S':
                seed = 1;
                break;
            case 'A':
                cluster_mode = 1;
                seed = 0;
                seed_addr = optarg;
            case 'P':
                cluster_mode = 1;
                seed = 0;
                seed_port = optarg;
            default:
                seed = 0;
                cluster_mode = 0;
                break;
        }
    }

    /* If cluster mode is enabled Initialize cluster map */
    if (cluster_mode == 1) {
        if (seed == 1) {
            /* map *cluster = map_create(); */
            char *self = NULL;
            sprintf(self, "%s:%s", address, port);
            /* int self_fd = -1; */
            /* map_put(cluster, self, &self_fd); */

            /* initialize two sockets:
             * - one for incoming client connections
             * - a second for intercommunication between nodes
             */
            int sockets[2] = {
                listento(address, "9999"),
                listento(address, "19999")
            };

            cluster_init(1);

            /* MUST HANDLE CLUSTER WIDE */

            /* #<{(| handler function for incoming data |)}># */
            /* fd_handler handler_ptr = &command_handler; */
            /*  */
            /* event_loop(sockets, 2, m, handler_ptr); */

        } else {

            /* MUST CONNECT TO A SEED, CLUSTER WIDE */

        }
    } else {

        cluster_init(0);
        /* initialize two sockets:
         * - one for incoming client connections
         * - a second for intercommunication between nodes
         */
        int sockets[2] = {
            listento(address, "9999"),
            listento(address, "19999")
        };

        /* handler function for incoming data */
        fd_handler handler_ptr = &command_handler;

        event_loop(sockets, 2, handler_ptr);

    }

    return 0;
}
