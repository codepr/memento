#include "cluster.h"
#include "partition.h"
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
                printf("<*> Node %d has slots -> [%d - %d]\n", m->fd, m->min, m->max);
            }
        }
}

/*
 * Private function needed to add the master node, used by slave nodes
 */
static void add_master_node(map_t cluster_members, int fd, int min, int max) {
    struct member *m = (struct member *) malloc(sizeof(struct member));
    m->min = min;
    m->max = max;
    m->fd = fd;
    m_put(cluster_members, "0", m);
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
    printf("<*> New node joined - cluster size -> %d\n", m_length(cluster_members));
    slot_distribution(cluster_members);
    void *val = NULL;
    m_get(cluster_members, "0", &val);
    struct member *master = (struct member *) val;
    char cmd[15];
    sprintf(cmd, "MASTER %d %d\n", master->min, master->max);
    h_map* map = (h_map *) cluster_members;
    for(int i = 0; i < map->table_size; i++) {
        if (map->data[i].in_use != 0 && (strcmp(map->data[i].key, "0")) != 0) {
            struct member *curr = (struct member *) map->data[i].data;
            if ((write(curr->fd, cmd, strlen(cmd))) == -1)
                perror("WRITE");
        }
    }
}

/*
 * Join the cluster, used by slave nodes to set a connection with the master
 * node
 */
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

    char cmd[7] = "ADDNODE";
    /* fcntl(sock_fd, F_SETFL, O_NONBLOCK); */
    if ((write(sock_fd, cmd, strlen(cmd))) == -1)
        perror("WRITE");

    while(read(sock_fd, buf, CMD_BUFSIZE) > 0) {
        printf("%s", buf);
        if (strncasecmp(buf, "master", 6) == 0) {
            int min = to_int(strtok(buf, " "));
            int max = to_int(strtok(buf, " "));
            add_master_node(cluster_members, sock_fd, min, max);
        }
        bzero(buf, CMD_BUFSIZE);
    }
}

/*
 * Master command router
 */
void cluster_route_command(map_t cluster, const char *command, int fd) {
    char buf[CMD_BUFSIZE];
    char node_key[3];
    sprintf(node_key, "%d", fd);
    void *val = NULL;
    m_get(cluster, node_key, &val);
    struct member *node = (struct member *) val;
    if ((write(node->fd, command, strlen(command))) == -1)
        perror("WRITE");
    // wait for answer
    while(read(node->fd, buf, CMD_BUFSIZE) > 0)
        printf("%s", buf);
}

void cluster_send_command(map_t cluster, const char *command) {
    void *val = NULL;
    m_get(cluster, "0", &val);
    struct member *master = (struct member *) val;
    int fd = master->fd;
    if ((write(fd, command, strlen(command))) == -1)
        perror("WRITE");
}
