/*
 * Copyright (c) 2016-2017 Andrea Giacomo Baldan
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef MAP_H
#define MAP_H

#include "queue.h"

#define MAP_OK 0
#define MAP_ERR -1
#define MAP_FULL -2
#define INITIAL_SIZE (256)
#define MAX_CHAIN_LENGTH (8)
#define SUBSCRIBER_SIZE (64)


typedef int (*func)(void *, void *);


/* We need to keep keys and values */
typedef struct {
    void *key;
    void *val;
    unsigned int in_use : 1;
    unsigned int has_expire_time : 1;
    long creation_time;
    long expire_time;
    int *subscribers;
    unsigned int last_subscriber;
    queue *data_history;
} map_entry;


/*
 * An hashmap has some maximum size and current size, as well as the data to
 * hold.
 */
typedef struct {
    unsigned int table_size;
    unsigned int size;
    map_entry *entries;
} map;


/* Map API */
map *map_create(void);
void map_release(map *);
int map_put(map *, void *, void *);
void *map_get(map *, void *);
map_entry *map_get_entry(map *, void *);
int map_del(map *, void *);
int map_iterate(map *, func, void *);
// int m_prefscan(map_t, func, any_t, int);
// int m_fuzzyscan(map_t, func, any_t, int);
// int m_get_map_entry(map_t, char *, map_entry *);
// int m_set_expire_time(map_t, char *, long);
// int m_remove(map_t, char *);
// int m_sub(map_t, char *, int);
// int m_sub_from(map_t, char *, int, int);
// int m_unsub(map_t, char *, int);
// int m_pub(map_t, char *, any_t);
// int m_length(map_t);

#endif
