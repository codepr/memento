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

#define MAP_MISSING -3
#define MAP_FULL -2
#define MAP_OMEM -1
#define MAP_OK 0
#define INITIAL_SIZE (256)
#define MAX_CHAIN_LENGTH (8)
#define SUBSCRIBER_SIZE (64)

typedef void *any_t;

typedef any_t map_t;

typedef int (*func)(any_t, any_t);

/* We need to keep keys and values */
typedef struct _kv_pair {
    char *key;
    unsigned int in_use : 1;
    unsigned int has_expire_time : 1;
    long creation_time;
    long expire_time;
    any_t data;
    int *subscribers;
    int last_subscriber;
    queue *data_history;
} kv_pair;

/* A hashmap has some maximum size and current size, as well as the data to
 * hold. */
typedef struct _h_map {
    int table_size;
    int size;
    kv_pair *data;
} h_map;

map_t m_create(void);
void m_release(map_t);
int m_iterate(map_t, func, any_t);
int m_prefscan(map_t, func, any_t, int);
int m_fuzzyscan(map_t, func, any_t, int);
int m_put(map_t, char *, any_t);
int m_get(map_t, char *, any_t *);
int m_get_kv_pair(map_t, char *, kv_pair *);
int m_set_expire_time(map_t, char *, long);
int m_remove(map_t, char *);
int m_sub(map_t, char *, int);
int m_sub_from(map_t, char *, int, int);
int m_unsub(map_t, char *, int);
int m_pub(map_t, char *, any_t);
int m_length(map_t);

#endif
