#include <string.h>
#include "util.h"
#include "partition.h"

partition *create_partition(void) {
    partition *p = (partition *) malloc(sizeof(partition));
    p->map = m_create();
    p->lower_bound = 0;
    p->upper_bound = PARTITION_NUMBER;
    return p;
}

/* deallocate map inside the partition */
int partition_release(partition *p) {
    if (p)
        m_release(p->map);
    free(p);
    return 0;
}

/* retrieve a valid index to distribute key inside a partitions array */
int partition_hash(char *keystring) {

    unsigned long key = crc32((unsigned char *) (keystring), strlen(keystring));

    /* Robert Jenkins' 32 bit Mix Function */
    key += (key << 12);
    key ^= (key >> 22);
    key += (key << 4);
    key ^= (key >> 9);
    key += (key << 10);
    key ^= (key >> 2);
    key += (key << 7);
    key ^= (key >> 12);

    /* Knuth's Multiplicative Method */
    key = (key >> 3) * 2654435761;

    return key % PARTITION_NUMBER;

}
