#ifndef PARTITION_H
#define PARTITION_H

#include "map.h"

#define PARTITION_NUMBER (1024)

typedef struct _partition {
    h_map *map;
    unsigned int lower_bound;
    unsigned int upper_bound;
} partition;

partition *create_partition(void);
int partition_release(partition *);
int partition_hash(char *);

#endif
