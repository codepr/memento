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
/* #include <string.h> */
/* #include <netdb.h> */
/* #include <fcntl.h> */
/* #include <time.h> */
#include "list.h"
#include "cluster.h"
#include "commands.h"


shibui instance;

int cluster_init(int distributed) {
    instance.cluster_mode = distributed;
    instance.store = map_create();
    instance.cluster = list_create();
    return 0;
}


/* void cluster_start(int cluster_mode, int *fds, */
/*         size_t len, map *store, map *members) { */
/*  */
/*     fd_handler handler_ptr = &command_handler; */
/*     event_loop(cluster_mode, fds, len, store, handler_ptr); */
/*  */
/* } */

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


/* struct cluster_member { */
/*     const char *name; */
/*     const char *host; */
/*     int port; */
/*     int master; */
/* }; */
