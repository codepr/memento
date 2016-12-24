#ifndef CLUSTER_H
#define CLUSTER_H

#include "map.h"

struct member {
    int fd;
    int min;
    int max;
};

void cluster_add_node(map_t, int);
void cluster_join(map_t, const char *, const char *);
void cluster_send_command(map_t, const char *);

#endif
