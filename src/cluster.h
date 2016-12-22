#ifndef CLUSTER_H
#define CLUSTER_H

#include "queue.h"

struct member {
    int fd;
    int min;
    int max;
};

void cluster_add_node(queue *, int);
void cluster_join(queue *, const char *, const char *);

#endif
