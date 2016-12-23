#include <stdio.h>
#include <getopt.h>
#include <pthread.h>
#include "server.h"
#include "cluster.h"

struct connection {
    const char *address;
    const char *port;
    map_t *cluster;
};

static void *cluster_pthread(void *param) {
    struct connection *conn = (struct connection *) param;
    const char *address = conn->address;
    const char *port = conn->port;
    map_t *cluster = conn->cluster;
    start_server(cluster, address, port);
    return NULL;
}

static void *cluster_join_pthread(void *param) {
    struct connection *conn = (struct connection *) param;
    const char *address = conn->address;
    const char *port = conn->port;
    map_t *cluster = conn->cluster;
    cluster_join(cluster, address, port);
    return NULL;
}

int main(int argc, char **argv) {
    char *address = "127.0.0.1";
    char *port = PORT;
    static pthread_t t;
    int opt;
    int master = 1;
    map_t *cluster = m_create();

    while((opt = getopt(argc, argv, "a:p:s")) != -1) {
        switch(opt) {
            case 'a':
                address = optarg;
                break;
            case 'p':
                port = optarg;
                break;
            case 's':
                master = 0;
                break;
            default:
                break;
        }
    }

    struct connection *conn = (struct connection *) malloc(sizeof(struct connection));
    conn->address = address;
    conn->port = "9999";
    conn->cluster = cluster;

    if (master == 1) {
        if (pthread_create(&t, NULL, &cluster_pthread, conn) != 0)
            perror("ERROR pthread");
        cluster_add_node(cluster, 0);
        start_server(cluster, address, port);
    } else {
        if (pthread_create(&t, NULL, &cluster_join_pthread, conn) != 0)
            perror("ERROR pthread");
        start_server(cluster, address, port);
    }

    return 0;
}
