#include "cluster.h"
#include "partition.h"
#include "commands.h"
#include "serializer.h"
#include "util.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>

#define CMD_BUFSIZE 1024


/*
 * Join the cluster, used by slave nodes to set a connection with the master
 * node
 */
void cluster_join(int distributed, partition **buckets, const char *hostname, const char *port) {
    int p = atoi(port);
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
        perror("ERROR connecting");
        exit(0);
    }

    char *buf = (char *) malloc(CMD_BUFSIZE);
    bzero(buf, CMD_BUFSIZE);
    int done = 0;
    int id, slave_number = 0;

    while(1) {
        while(read(sock_fd, buf, CMD_BUFSIZE) > 0) {
            struct message mm = deserialize(buf);
            char *m = mm.content;
            if (m[0] == '#') {
                // first connection, set number of slaves and number assigned
                id = to_int(m + 1); // first position handle id
                slave_number = to_int(m + 2); // second position handle slave number
                printf("<*> Refreshed Node ID: %d and total slaves: %d\n", id, slave_number);
            } else {
                printf("<*> Request from master -> %s - %d\n", mm.content, mm.fd);
                int proc = process_command(distributed, buckets, m, sock_fd, mm.fd);
                /* int proc = process_command(distributed, buckets, m, mm.fd); */
                struct message to_be_sent;
                to_be_sent.fd = mm.fd;
                switch(proc) {
                    case MAP_OK:
                        to_be_sent.content = "* OK\n";
                        send(sock_fd, serialize(to_be_sent), 13, 0); // 13 = content size + sizeof(int) * 2 for metadata and fd
                        break;
                    case MAP_MISSING:
                        to_be_sent.content = "* NOT FOUND\n";
                        send(sock_fd, serialize(to_be_sent), 19, 0);
                        break;
                    case MAP_FULL:
                    case MAP_OMEM:
                        to_be_sent.content = "* OUT OF MEMORY\n";
                        send(sock_fd, serialize(to_be_sent), 23, 0);
                        break;
                    case COMMAND_NOT_FOUND:
                        to_be_sent.content = "* COMMAND NOT FOUND\n";
                        send(sock_fd, serialize(to_be_sent), 27, 0);
                        break;
                    case END:
                        done = 1;
                        break;
                    default:
                        continue;
                        break;
                }
                bzero(buf, CMD_BUFSIZE);
                if (done) {
                    printf("Closed connection on descriptor %d\n", sock_fd);
                    /* Closing the descriptor will make epoll remove it from the
                       set of descriptors which are monitored. */
                    close(sock_fd);
                }
            }
        }
        free(buf);
    }
}

