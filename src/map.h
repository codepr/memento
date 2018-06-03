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


#define MAP_OK              0
#define MAP_ERR             -1
#define MAP_FULL            -2


typedef int (*func)(void *, void *);
typedef int (*func3)(void *, void *, void *);


/* We need to keep keys and values */
typedef struct {
    void *key;
    void *val;
    unsigned int in_use : 1;
    unsigned int has_expire_time : 1;
    long creation_time;
    long expire_time;
} map_entry;


/*
 * An hashmap has some maximum size and current size, as well as the data to
 * hold.
 */
typedef struct {
    unsigned long table_size;
    unsigned long size;
    map_entry *entries;
} map;


/* Map API */
map *map_create(void);
void map_release(map *);
int map_put(map *, void *, void *);
void *map_get(map *, void *);
map_entry *map_get_entry(map *, void *);
int map_del(map *, void *);
int map_iterate2(map *, func, void *);
int map_iterate3(map *, func3, void *, void *);


#endif
