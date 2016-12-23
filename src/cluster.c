#include "cluster.h"
#include "partition.h"
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
 * Retrieve cluster size and values of members from cluster_members then we
 * have to partition 1024 slots equally
 */
static void slot_distribution(map_t cluster_members) {
    h_map* map = (h_map *) cluster_members;
    int counter = 0;
    int step = PARTITION_NUMBER / m_length(cluster_members);
    // iterate through the keyspace
    for(int i = 0; i < map->table_size; i++)
        if (map->data[i].in_use != 0) {
            if (map->data[i].data != NULL) {
                struct member *m = (struct member *) map->data[i].data;
                m->min = step * counter;
                m->max = (m->min + step) - 1;
                counter++;
                printf("<*> NODE %d HAS RANGE -> %d - %d\n", m->fd, m->min, m->max);
            }
        }
}

/*
 * Add a new node to the cluster, currently tracked only on master node (temporary)
 */
void cluster_add_node(map_t cluster_members, int fd) {
    struct member *m = (struct member *) malloc(sizeof(struct member));
    m->min = 0;
    m->max = PARTITION_NUMBER;
    m->fd = fd;
    char key[3];
    sprintf(key, "%d", fd);
    m_put(cluster_members, key, m);
    printf("<*> NEW NODE JOINED - CLUSTER SIZE -> %d\n", m_length(cluster_members));
    slot_distribution(cluster_members);
}

void cluster_join(map_t cluster_members, const char *hostname, const char *port) {
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

    /* cluster_add_node(cluster_members, sock_fd); */
    char cmd[7] = "ADDNODE";
    /* char cmd[10]; */
    /* sprintf(cmd, "ADDNODE %d\n", sock_fd); */
    /* fcntl(sock_fd, F_SETFL, O_NONBLOCK); */
    if ((write(sock_fd, cmd, strlen(cmd))) == -1)
        perror("WRITE");

    while(read(sock_fd, buf, CMD_BUFSIZE) > 0) {
        printf("%s", buf);
        bzero(buf, CMD_BUFSIZE);
    }

}
