#ifndef MAP_H
#define MAP_H

#define MAP_MISSING -3
#define MAP_FULL -2
#define MAP_OMEM -1
#define MAP_OK 0

typedef void *any_t;

typedef any_t map_t;

typedef int (*func)(any_t, any_t);

/* We need to keep keys and values */
typedef struct _kv_pair {
    char *key;
    int in_use;
    any_t data;
    int *subscribers;
    int last_subscriber;
} kv_pair;

/* A hashmap has some maximum size and current size,
 * as well as the data to hold. */
typedef struct _h_map {
    int table_size;
    int size;
    kv_pair *data;
} h_map;

map_t m_create();
int m_iterate(map_t, func, any_t);
int m_put(map_t, char *, any_t);
int m_get(map_t, char *, any_t *);
int m_remove(map_t, char *);
int m_get_one(map_t, any_t *, int);
int m_sub(map_t, char *, int);
int m_pub(map_t, char *, any_t);
void m_release(map_t);
int m_length(map_t);

#endif
