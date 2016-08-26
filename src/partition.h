#ifndef PARTITION_H
#define PARTITION_H

#include "map.h"

#define PARTITION_NUMBER (1024)

typedef struct _partition {
    h_map *map;
} partition;

partition *create_partition();
int partition_release(partition *);
int partition_hash(char *);

#endif
