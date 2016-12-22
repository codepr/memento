#include "cluster.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>

#define CMD_BUFSIZE 1024

void cluster_add_node(queue *cluster_members, int fd) {
    struct member *m = (struct member *) malloc(sizeof(struct member));
    m->min = 0;
    m->max = 1024;
    int *file_descriptor = (int *) malloc(sizeof(int));
    *file_descriptor = fd;
    m->fd = *file_descriptor;
    if ((queue_contains(cluster_members, m)) == -1) {
        printf("Added %d\n", fd);
        enqueue(cluster_members, m);
        char cmd[7] = "ADDNODE";
        for (int i = 0; i < queue_len(cluster_members); ++i) {
            int desc = ((struct member *) (((cluster_members->rear)+i)->data))->fd;
            if (desc != fd) {
                printf("Comanding to %d\n", desc);
                if ((write(desc, cmd, strlen(cmd))) == -1)
                    perror("WRITE");
            }
        }
    }
}

void cluster_join(queue *cluster_members, const char *hostname, const char *port) {
    int p = atoi(port);
    char buf[CMD_BUFSIZE];
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

    cluster_add_node(cluster_members, sock_fd);
    char cmd[7] = "ADDNODE";
    fcntl(sock_fd, F_SETFL, O_NONBLOCK);
    if ((write(sock_fd, cmd, strlen(cmd))) == -1)
        perror("WRITE");

    while(read(sock_fd, buf, CMD_BUFSIZE) > 0) {
        printf("%s", buf);
        bzero(buf, CMD_BUFSIZE);
    }

}
