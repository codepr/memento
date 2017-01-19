#include <stdio.h>
#include <getopt.h>
#include <pthread.h>
#include "server.h"
#include "cluster.h"
#include "queue.h"
#include "messagequeue.h"

struct connection {
    const char *address;
    const char *port;
    int distributed;
    queue *mqueue;
    partition **buckets;
};

static void *cluster_pthread(void *param) {
    struct connection *conn = (struct connection *) param;
    /* const char *address = conn->address; */
    /* const char *port = conn->port; */
    queue *mqueue = conn->mqueue;
    mq_seed_gateway(mqueue);
    return NULL;
}

static void *cluster_join_pthread(void *param) {
    struct connection *conn = (struct connection *) param;
    const char *address = conn->address;
    const char *port = conn->port;
    partition **buckets = conn->buckets;
    int distributed = conn->distributed;
    cluster_join(distributed, buckets, address, port);
    return NULL;
}

int main(int argc, char **argv) {
    char *address = "127.0.0.1";
    char *port = PORT;
    static pthread_t t;
    int opt;
    int master = 0;
    int distributed = 0;
    queue *mqueue = create_queue();
    partition **buckets = (partition **) malloc(sizeof(partition) * PARTITION_NUMBER);
    for (int i = 0; i < PARTITION_NUMBER; i++)
        buckets[i] = create_partition();

    while((opt = getopt(argc, argv, "a:p:ms")) != -1) {
        switch(opt) {
            case 'a':
                address = optarg;
                break;
            case 'p':
                port = optarg;
                break;
            case 'm':
                master = 1;
                distributed = 1;
                break;
            case 's':
                master = 0;
                distributed = 1;
                break;
            default:
                distributed = 0;
                master = 0;
                break;
        }
    }

    struct connection *conn = (struct connection *) malloc(sizeof(struct connection));
    conn->address = address;
    conn->port = "9898";
    conn->mqueue = mqueue;
    conn->buckets = buckets;
    conn->distributed = distributed;

    if (distributed == 1) {
        if (master == 1) {
            if (pthread_create(&t, NULL, &cluster_pthread, conn) != 0)
                perror("ERROR pthread");
            start_server(mqueue, 1, distributed, buckets, address, port);
        } else {
            if (pthread_create(&t, NULL, &cluster_join_pthread, conn) != 0)
                perror("ERROR pthread");
            start_server(mqueue, 0, distributed, buckets, address, port);
        }
    } else start_server(mqueue, 0, 0, buckets, address, port);

    return 0;
}
