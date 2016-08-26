#ifndef PARTITION_H
#define PARTITION_H

#include "util.h"
#include "server.h"

#define PARTITION_NUMBER (1024)

typedef struct _partition {
    int in_use;
    h_map *map;
} partition;

partition *create_partition();
int partition_release(partition *);
int partition_hash(char *);

#endif
