#include <string.h>
#include "partition.h"

partition *create_partition() {
    partition *p = (partition *) malloc(sizeof(partition));
    p->in_use = 0;
    p->map = m_create();
    return p;
}


int partition_release(partition *p) {
    if (p) {
        p->in_use = 0;
        m_release(p->map);
    }
    free(p);
    return 0;
}

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
